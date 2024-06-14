/*************************************************************************
    > File Name: kcp_benchmark.cc
    > Author: hsz
    > Brief:
    > Created Time: Wed 20 Jul 2022 01:44:12 PM CST
 ************************************************************************/

#include "../kcpmanager.h"
#include <assert.h>
#include <iostream>
#include <signal.h>
#include <log/log.h>
#include <log/callstack.h>

using namespace std;

#define SERVER_PORT 12000
#define LOG_TAG     "kcp-bench-mark"

int createSocket()
{
    int server_fd, ret;
    sockaddr_in addr;
    socklen_t len = sizeof(sockaddr_in);

    server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        perror("create socket fail!");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_PORT);

    ret = bind(server_fd, (struct sockaddr*)&addr, len);
    if(ret < 0) {
        perror("socket bind fail!");
        return -1;
    }

    int reuse = true;
    assert(0 == setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));

    return server_fd;
}

static std::atomic<uint32_t> gRecvSize{0};
static const uint16_t timeout = 500;

void onReadEvent(Kcp *kcp, ByteBuffer &buffer, sockaddr_in addr)
{
    // LOGD("%s() [%s:%d]", __func__, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    gRecvSize += buffer.size();
}

void onTimerEvent(Kcp *kcp)
{
    uint32_t recvSize = gRecvSize;
    gRecvSize = 0;
    recvSize *= 2;
    if (recvSize / 1000 / 1000 > 0) {
        LOGW("onTimerEvent() %d Mb/s", recvSize / 1000 / 1000);
    } else if (recvSize / 1000 > 0) {
        LOGW("onTimerEvent() %d Kb/s", recvSize / 1000);
    } else {
        LOGW("onTimerEvent() %d b/s", recvSize);
    }
}

void signalCatch(int sig)
{
    CallStack stack;
    stack.update();
    stack.log(LOG_TAG, eular::LogLevel::LEVEL_FATAL);

    exit(0);
}

int main(int argc, char **argv)
{
    signal(SIGSEGV, signalCatch);
    signal(SIGABRT, signalCatch);

    eular::log::InitLog(LogLevel::LEVEL_INFO);

    KcpManager *manager = KcpManagerInstance::Get(1, true, "test_kcp_server");

    int udp = createSocket();
    assert(udp > 0);
    sockaddr_in addr;
    socklen_t len = sizeof(addr);

    KcpAttr attr;
    attr.fd = udp;
    attr.autoClose = true;
    attr.conv = 0x1024;
    attr.interval = 10;
    attr.addr = addr;
    attr.nodelay = 1;
    attr.fastResend = 2;
    attr.sendWndSize = 10240;
    attr.recvWndSize = 10240;

    Kcp::SP kcp(new Kcp(attr));
    kcp->installRecvEvent(std::bind(onReadEvent, kcp.get(), std::placeholders::_1, std::placeholders::_2));

    manager->addKcp(kcp);
    manager->addTimer(timeout, std::bind(onTimerEvent, kcp.get()), timeout);
    KcpManager::GetMainFiber()->resume();

    return 0;
}
