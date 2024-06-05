/*************************************************************************
    > File Name: test_kcp_server.cc
    > Author: hsz
    > Brief:
    > Created Time: Thu 14 Jul 2022 09:34:05 AM CST
 ************************************************************************/

#include "../kcpmanager.h"
#include <assert.h>
#include <iostream>
#include <signal.h>
#include <log/log.h>
#include <log/callstack.h>

#define LOG_TAG "test-kcp-server"

using namespace std;

#define SERVER_PORT 12000

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
    if (ret < 0) {
        perror("socket bind fail!");
        return -1;
    }

    int reuse = true;
    LOG_ASSERT2(0 == setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));

    return server_fd;
}

void onReadEvent(Kcp *kcp, ByteBuffer &buffer, sockaddr_in addr)
{
    LOGI("%s() [%s](%zu) [%s:%d]", __func__, (char *)buffer.data(), buffer.size(), inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    uint8_t buf[128] = {0};
    sprintf((char *)buf, "RECV %zuBytes", buffer.size());
    kcp->send(ByteBuffer(buf, strlen((char *)buf)));
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

    KcpManager *manager = KcpManagerInstance::get(1, true, "test_kcp_server");

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
    KcpManager::GetMainFiber()->resume();

    return 0;
}
