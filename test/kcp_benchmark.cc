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
#define SERVER_IP   "10.0.24.17"
#define SERVER_PORT 12000
#define LOG_TAG     "kcp-bench-mark"

int createSocket()
{
    int server_fd, ret;
    sockaddr_in addr;
    socklen_t len = sizeof(sockaddr_in);

    server_fd = socket(AF_INET, SOCK_DGRAM, 0); // AF_INET:IPV4;SOCK_DGRAM:UDP
    if (server_fd < 0) {
        perror("create socket fail!");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(SERVER_IP); // IP地址，需要进行网络序转换，INADDR_ANY：本地地址
    addr.sin_port = htons(SERVER_PORT);  // 端口号，需要网络序转换

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
    LOGI("%s() [%s](%zu) [%s:%d]", __func__, (char *)buffer.data(), buffer.size(), inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    gRecvSize += buffer.size();
}

void onTimerEvent(Kcp *kcp)
{
    LOGD("onTimerEvent() %d b/s", gRecvSize * timeout / 1000);
    gRecvSize = 0;
}

void signalCatch(int sig)
{
    CallStack stack;
    stack.update();
    stack.log(LOG_TAG, eular::LogLevel::FATAL);

    exit(0);
}

int main(int argc, char **argv)
{
    signal(SIGSEGV, signalCatch);
    signal(SIGABRT, signalCatch);

    KcpManager *manager = KcpManagerInstance::get(1, "test_kcp_server");

    int udp = createSocket();
    assert(udp > 0);
    sockaddr_in addr;
    socklen_t len = sizeof(addr);

    KcpAttr attr;
    attr.fd = udp;
    attr.autoClose = true;
    attr.conv = 0x1024;
    attr.interval = 20;
    attr.addr = addr;
    attr.nodelay = 1;
    attr.fastResend = 2;
    attr.sendWndSize = 10240;
    attr.recvWndSize = 10240;

    Kcp::SP kcp(new Kcp(attr));
    kcp->installRecvEvent(std::bind(onReadEvent, kcp.get(), std::placeholders::_1, std::placeholders::_2));

    manager->addKcp(kcp);
    manager->addTimer(timeout, std::bind(onTimerEvent, kcp.get()), timeout);
    manager->start(true);

    return 0;
}
