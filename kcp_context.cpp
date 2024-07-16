/*************************************************************************
    > File Name: kcp_context.cpp
    > Author: hsz
    > Brief:
    > Created Time: Thu 06 Jun 2024 10:41:39 AM CST
 ************************************************************************/

#include "kcp_context.h"
#include <log/log.h>

#include "ikcp.h"
#include "ktimer.h"
#include "kcp_utils.h"
#include "kcp_protocol.h"

#define LOG_TAG "KcpContext"

#define CACHE_SIZE  8 * 1024

#define BUFFER_MAX      (4 * 1024 * 1024)
#define BUFFER_DEFAULE  (32 * 1024)
#define BUFFER_MIN      (4 * 1024)

namespace eular {
KcpContext::KcpContext() :
    m_timerId(0),
    m_kcpHandle(nullptr),
    m_recvBuffer(CACHE_SIZE)
{
}

KcpContext::~KcpContext()
{
    reset();
}

void KcpContext::setSetting(const KcpSetting &setting)
{
    reset();
    m_setting = setting;

    char ipv4Host[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &m_setting.remote_addr.sin_addr, ipv4Host, INET_ADDRSTRLEN);
    m_peerHost = ipv4Host;
    m_peerPort = ntohs(m_setting.remote_addr.sin_port);

    m_kcpHandle = ikcp_create(setting.conv, this);
    ikcp_setoutput(m_kcpHandle, &KcpContext::KcpOutput);
    ikcp_wndsize(m_kcpHandle, setting.send_wnd_size, setting.recv_wnd_size);
    ikcp_nodelay(m_kcpHandle, setting.nodelay, setting.interval, setting.fast_resend, setting.nocwnd);
}

void KcpContext::installRecvEvent(ReadEventCB onRecvEvent)
{
    m_recvEvent = onRecvEvent;
}

void KcpContext::setSendBufferSize(uint32_t size)
{
    AutoLock<Mutex> lock(m_bufMutex);
    if (size < BUFFER_MIN) {
        size = BUFFER_MIN;
    } else if (size > BUFFER_MAX) {
        size = BUFFER_MAX;
    }

    m_sendBuffer.reserve(size);
}

uint32_t KcpContext::bufferCapacity()
{
    AutoLock<Mutex> lock(m_bufMutex);
    return static_cast<uint32_t>(m_sendBuffer.capacity());
}

uint32_t KcpContext::bufferSize()
{
    AutoLock<Mutex> lock(m_bufMutex);
    return static_cast<uint32_t>(m_sendBuffer.size());
}

bool KcpContext::send(const void *buffer, uint32_t size)
{
#ifdef USE_BUFFER_QUEUE
    ByteBuffer buf((const uint8_t *)buffer, size);
    return m_sendBufQueue.enqueue(std::move(buf));
#else
    if (nullptr == buffer || 0 == size) {
        return false;
    }

    AutoLock<Mutex> lock(m_bufMutex);
    if (eular_unlikely(m_sendBuffer.capacity() == 0)) {
        m_sendBuffer.reserve(BUFFER_DEFAULE);
    }

    uint32_t capacity = static_cast<uint32_t>(m_sendBuffer.capacity());
    uint32_t bufSize = static_cast<uint32_t>(m_sendBuffer.size());
    LOG_ASSERT2(capacity >= bufSize);

    // 防止append自动扩容
    if ((capacity - bufSize) < size) {
        return false;
    }

    m_sendBuffer.append(static_cast<const uint8_t *>(buffer), size);
    return true;
#endif
}

bool KcpContext::send(const eular::ByteBuffer &buffer)
{
#ifdef USE_BUFFER_QUEUE
    return m_sendBufQueue.enqueue(buffer);
#else
    return this->send(buffer.const_data(), buffer.size());
#endif
}

bool KcpContext::send(eular::ByteBuffer &&buffer)
{
#ifdef USE_BUFFER_QUEUE
    return m_sendBufQueue.enqueue(std::forward<eular::ByteBuffer>(buffer));
#else
    return this->send(buffer.const_data(), buffer.size());
#endif
}

void KcpContext::closeContext()
{
    if (m_closeEvent) {
        m_closeEvent(shared_from_this(), true);
        m_closeEvent = nullptr;
    }
}

void KcpContext::resetContext()
{
    if (m_setting.conv > 0) {
        if (m_closeEvent) {
            m_closeEvent(shared_from_this(), false);
            m_closeEvent = nullptr;
        }

        m_setting.conv = 0;
    }
}

const String8& KcpContext::getLocalHost() const
{
    return m_localHost;
}

uint16_t KcpContext::getLocalPort() const
{
    return m_localPort;
}

const String8& KcpContext::getPeerHost() const
{
    return m_peerHost;
}

uint16_t KcpContext::getPeerPort() const
{
    return m_peerPort;
}

int KcpContext::KcpOutput(const char *buf, int len, IKCPCB *kcp, void *user)
{
    KcpContext *pKcpCtx = static_cast<KcpContext *>(user);
    LOGD("kcp callback. conv: %#x sendto [%s] size = %d", pKcpCtx->m_setting.conv,
        utils::Address2String((sockaddr *)&pKcpCtx->m_setting.remote_addr), len);

    if (buf && len > 0) {
        return ::sendto(pKcpCtx->m_setting.fd, buf, len, 0, (sockaddr *)&pKcpCtx->m_setting.remote_addr, sizeof(sockaddr_in));
    }

    return 0;
}

void KcpContext::onUpdateTimeout()
{
#ifdef USE_BUFFER_QUEUE
    eular::ByteBuffer buffer;
    while (m_sendBufQueue.try_dequeue(buffer)) {
        int32_t status = ikcp_send(m_kcpHandle, (const char *)(buffer.const_data()), buffer.size());
        if (status < 0) {
            LOGE("ikcp_send error. %d", status);
            break;
        }
    }
#else
    // 一次最多发送大小
    uint32_t maxSendSize = (MTU_SIZE - KCP_HEADER_SIZE) * 128; // IKCP_WND_RCV
    uint32_t alreadySendSize = 0;   // 已发送大小
    uint32_t sendSize = 0;          // 一次发送大小
    {
        AutoLock<Mutex> lock(m_bufMutex);
        do {
            // 剩余可发送大小
            sendSize = static_cast<uint32_t>(m_sendBuffer.size()) - alreadySendSize;
            // 一次最大发送大小
            sendSize = sendSize > maxSendSize ? maxSendSize : sendSize;
            const char *pBufferBegin = (const char *)(m_sendBuffer.const_data() + alreadySendSize);
            int32_t status = ikcp_send(m_kcpHandle, pBufferBegin, sendSize);
            if (status < 0) {
                LOGE("ikcp_send error. %d", status);
                break;
            }
            alreadySendSize += sendSize;
        } while (false);

        if (alreadySendSize > 0) {
            uint32_t size = m_sendBuffer.size();
            if (size == alreadySendSize) {
                m_sendBuffer.clear();
            } else { // alreadySendSize < size
                const uint8_t *pStart = m_sendBuffer.const_data() + alreadySendSize;
                m_sendBuffer.set(pStart, size - alreadySendSize);
            }
        }
    }
#endif

    ikcp_update(m_kcpHandle, static_cast<uint32_t>(KTimer::CurrentTime()));
}

void KcpContext::onRecv(const eular::ByteBuffer &inputBuffer)
{
    int32_t errorCode = ikcp_input(m_kcpHandle, (const char *)(inputBuffer.const_data()), inputBuffer.size());
    if (errorCode < 0) {
        LOGE("ikcp_input error. %d", errorCode);
        return;
    }

    int32_t peekSize = ikcp_peeksize(m_kcpHandle);
    if (peekSize <= 0) {
        return;
    }

    m_recvBuffer.reserve(peekSize);
    m_recvBuffer.clear();
    int32_t nrecv = ikcp_recv(m_kcpHandle, (char *)m_recvBuffer.data(), peekSize);
    LOGD("ikcp_recv size %d", nrecv);
    if (nrecv > 0) {
        m_recvBuffer.resize(nrecv);
        if (m_recvEvent) {
            m_recvEvent(shared_from_this(), m_recvBuffer);
        }
    }
}

void KcpContext::reset()
{
    if (m_kcpHandle != nullptr) {
        ikcp_release(m_kcpHandle);
        m_kcpHandle = nullptr;
    }
}
} // namespace eular
