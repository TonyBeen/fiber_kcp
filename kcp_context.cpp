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

#define LOG_TAG "KcpContext"

#define CACHE_SIZE  8 * 1024

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

bool KcpContext::send(eular::ByteBuffer &&buffer)
{
    return m_sendBufQueue.enqueue(std::forward<eular::ByteBuffer>(buffer));
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
    if (buf && len > 0) {
        LOGD("kcp callback. sendto [%s:%d]", inet_ntoa(pKcpCtx->m_setting.remote_addr.sin_addr), ntohs(pKcpCtx->m_setting.remote_addr.sin_port));
        return ::sendto(pKcpCtx->m_setting.fd, buf, len, 0, (sockaddr *)&pKcpCtx->m_setting.remote_addr, sizeof(sockaddr_in));
    }

    return 0;
}

void KcpContext::onUpdateTimeout()
{
    eular::ByteBuffer buffer;
    while (m_sendBufQueue.try_dequeue(buffer)) {
        int32_t status = ikcp_send(m_kcpHandle, (const char *)(buffer.const_data()), buffer.size());
        if (status < 0) {
            LOGE("ikcp_send error. %d", status);
            break;
        }
    }

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
