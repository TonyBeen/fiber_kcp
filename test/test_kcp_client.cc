/*************************************************************************
    > File Name: test_kcp_client.cc
    > Author: hsz
    > Brief:
    > Created Time: Thu 14 Jul 2022 10:11:15 AM CST
 ************************************************************************/

#include "../kcpmanager.h"
#include <assert.h>
#include <iostream>
#include <signal.h>
#include <stdio.h>
#include <log/log.h>
#include <log/callstack.h>

#define LOG_TAG "test-kcp-client"

using namespace std;

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 12000

void onReadEvent(ByteBuffer &buffer, sockaddr_in addr)
{
    LOGI("%s() %s [%s:%d]", __func__, (char *)buffer.data(), inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
}

void signalCatch(int sig)
{
    if (sig == SIGSEGV) {
        CallStack stack;
        stack.update();
        stack.log(LOG_TAG, eular::LogLevel::LEVEL_FATAL);
    }
    exit(0);
}

int main(int argc, char **argv)
{
    signal(SIGSEGV, signalCatch);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("create socket fail!");
        return -1;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    addr.sin_port = htons(SERVER_PORT);

    KcpAttr attr;
    attr.fd = fd;
    attr.autoClose = true;
    attr.conv = 0x1024;
    attr.interval = 20;
    attr.addr = addr;
    attr.nodelay = 1;
    attr.fastResend = 2;
    attr.sendWndSize = 10240;
    attr.recvWndSize = 10240;

    Kcp::SP kcp(new Kcp(attr));
    kcp->installRecvEvent(std::bind(onReadEvent, std::placeholders::_1, std::placeholders::_2));

    KcpManager *manager = KcpManagerInstance::Get(1, false, "test_kcp_client");
    manager->addKcp(kcp);

    char buf[128] = {0};
    uint16_t times = 0;
    while (true) {
        snprintf(buf, sizeof(buf), "Hello (times: %d)", ++times);
        kcp->send(ByteBuffer(buf, strlen(buf)));
        printf("send -> %s\n", buf);
        msleep(20); // 发送太快会使发送窗口缓存太多而不能把数据发出去
        if (times == 1024) {
            break;
        }
    }

    sleep(1);
    return 0;
}
