/*************************************************************************
    > File Name: fiber.cpp
    > Author: hsz
    > Brief:
    > Created Time: Mon 04 Jul 2022 11:25:32 AM CST
 ************************************************************************/

#include "kcp.h"
#include <utils/utils.h>
#include <utils/exception.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <log/log.h>

#define LOG_TAG "Kcp"

// TODO 增加心跳检测

Kcp::Kcp() :
    mKcpHandle(nullptr),
    mRecvEvent(nullptr)
{

}

Kcp::Kcp(const KcpAttr &attr) :
    mAttr(attr),
    mKcpHandle(nullptr),
    mRecvEvent(nullptr)
{
    if (init() == false) {
        throw eular::Exception("Kcp(const KcpAttr &attr) init error.");
    }
}

Kcp::~Kcp()
{
    if (mKcpHandle) {
        ikcp_release(mKcpHandle);
        mKcpHandle = nullptr;
    }
    mRecvEvent = nullptr;
    if (mAttr.autoClose) {
        close(mAttr.fd);
    }
}

bool Kcp::installRecvEvent(Callback onRecvEvent)
{
    mRecvEvent.swap(onRecvEvent);
    return true;
}

/**
 * @brief 发送数据。做缓存队列，如果直接调用ikcp_send时发的太快会使后面的数据丢失
 * 
 * @param buffer 
 */
void Kcp::send(const eular::ByteBuffer &buffer)
{
    eular::AutoLock<eular::Mutex> lock(mQueueMutex);
    mSendBufQueue.push_back(buffer);
}

bool Kcp::setAttr(const KcpAttr &attr)
{
    if (mKcpHandle) {
        return true;
    }
    mAttr = attr;
    return init();
}

uint32_t Kcp::check()
{
    return ikcp_check(mKcpHandle, Time::Abstime());
}

bool Kcp::init()
{
    mKcpHandle = ikcp_create(mAttr.conv, this);
    if (mKcpHandle == nullptr) {
        return false;
    }

    ikcp_setoutput(mKcpHandle, &Kcp::KcpOutput);
    ikcp_wndsize(mKcpHandle, mAttr.sendWndSize, mAttr.recvWndSize);
    ikcp_nodelay(mKcpHandle, mAttr.nodelay, mAttr.interval, mAttr.fastResend, 1);
    return true;
}

bool Kcp::create()
{
    mBindTid = gettid();
    return true;
}

int Kcp::KcpOutput(const char *buf, int len, ikcpcb *kcp, void *user)
{
    Kcp *__kcp = static_cast<Kcp *>(user);
    if (buf && len > 0) {
        LOGD("kcp callback. sendto [%s:%d] len %d", inet_ntoa(__kcp->mAttr.addr.sin_addr), ntohs(__kcp->mAttr.addr.sin_port), len);
        return ::sendto(__kcp->mAttr.fd, buf, len, 0, (sockaddr *)&__kcp->mAttr.addr, sizeof(sockaddr_in));
    }

    return 0;
}

void Kcp::inputRoutine()
{
    LOGD("----------> begin <----------");
    char buf[2 * 1400] = {0};
    sockaddr_in peerAddr;
    socklen_t len = sizeof(sockaddr_in);

    while (true) {
        int32_t nrecv = ::recvfrom(mAttr.fd, buf, sizeof(buf), 0, (sockaddr *)&peerAddr, &len);
        if (nrecv < 0) {
            if (errno != EAGAIN) {
                LOGE("recvfrom error. [%d,%s]", error, strerror(errno));
            }

            break;
        }
        LOGD("recvfrom [%s:%d] size %zu", inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port), nrecv);
        int32_t conv = ikcp_getconv(buf);
        if (conv != mAttr.conv)
        {
            LOGE("conv(%d) != self_conv(%d)", conv, mAttr.conv);
            continue;
        }
        mAttr.addr = peerAddr;

        int32_t ret = ikcp_input(mKcpHandle, buf, nrecv);
        if (ret < 0) {
            LOGE("ikcp_input error. %d", ret);
            continue;
        }
    }

    int32_t ret = ikcp_peeksize(mKcpHandle);
    if (ret > 0) {
        eular::ByteBuffer buffer(ret);
        int32_t nrecv = ikcp_recv(mKcpHandle, (char *)buffer.data(), ret);
        LOGD("ikcp_recv size %d", nrecv);
        if (nrecv > 0) {
            buffer.resize(nrecv);
            mRecvEvent(buffer, peerAddr);
        }
    }
    LOGD("----------> end <----------");
}

void Kcp::outputRoutine()
{
    std::list<eular::ByteBuffer> queue;
    {
        eular::AutoLock<eular::Mutex> lock(mQueueMutex);
        queue = std::move(mSendBufQueue);
    }

    for (auto it : queue) {
        int ret = ikcp_send(mKcpHandle, (const char *)(it.const_data()), it.size());
        if (ret < 0) {
            LOGE("ikcp_send error. %d", ret);
        }
    }

    ikcp_update(mKcpHandle, Time::Abstime());
}
