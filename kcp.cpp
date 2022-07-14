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

void Kcp::send(const eular::ByteBuffer &buffer)
{
    eular::AutoLock<eular::Mutex> lock(mQueueMutex);
    int ret = ikcp_send(mKcpHandle, (const char *)(buffer.const_data()), buffer.size());
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
    ikcp_nodelay(mKcpHandle, mAttr.nodelay, mAttr.interval, mAttr.fastResend, 0);
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
        LOGD("kcp callback. sendto [%s:%d]", inet_ntoa(__kcp->mAttr.addr.sin_addr), ntohs(__kcp->mAttr.addr.sin_port));
        return ::sendto(__kcp->mAttr.fd, buf, len, 0, (sockaddr *)&__kcp->mAttr.addr, sizeof(sockaddr_in));
    }

    return 0;
}

void Kcp::inputRoutine()
{
    eular::ByteBuffer buffer;
    uint8_t buf[256] = {0};
    sockaddr_in peerAddr;
    socklen_t len = sizeof(sockaddr_in);
    bool hasError = false;
    while (true) {
        int nrecv = ::recvfrom(mAttr.fd, buf, sizeof(buf), 0, (sockaddr *)&peerAddr, &len);
        if (nrecv < 0) {
            if (errno != EAGAIN) {
                LOGE("recvfrom error. [%d,%s]", error, strerror(errno));
                hasError = true;
            }
            break;
        }
        buffer.append(buf, nrecv);
    }

    if (!hasError) {
        int ret = ikcp_input(mKcpHandle, (char *)buffer.data(), buffer.size());
        if (ret < 0) {
            LOGE("ikcp_input error. %d", ret);
            return;
        }
        ret = ikcp_peeksize(mKcpHandle);
        LOGD("%s() %d can read", __func__, ret);
        if (ret < 0) {
            return;
        }
        buffer.resize(ret + 1);
        int nrecv = ikcp_recv(mKcpHandle, (char *)buffer.data(), ret);
        if (nrecv > 0) {
            buffer.setDataSize(ret);
            mRecvEvent(buffer, peerAddr);
        }
    }
}

void Kcp::outputRoutine()
{
    eular::AutoLock<eular::Mutex> lock(mQueueMutex);
    ikcp_update(mKcpHandle, Time::Abstime());
}
