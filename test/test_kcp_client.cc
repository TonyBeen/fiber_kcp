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
        stack.log(LOG_TAG, eular::LogLevel::FATAL);
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

    const char *hello = "CONNECT";
    sendto(fd, hello, strlen(hello), 0, (sockaddr *)&addr, sizeof(addr));

    uint16_t conv = 0;
    recvfrom(fd, &conv, sizeof(conv), 0, nullptr, nullptr);
    printf("conv = 0x%x\n", conv);

    KcpAttr attr;
    attr.fd = fd;
    attr.autoClose = true;
    attr.conv = conv;
    attr.interval = 50;

    Kcp::SP kcp(new Kcp(attr));
    kcp->installRecvEvent(std::bind(onReadEvent, std::placeholders::_1, std::placeholders::_2));

    KcpManager *manager = KcpManagerInstance::get(1, false, "test_kcp_client");
    manager->addKcp(kcp);

    char buf[128] = {0};
    uint16_t times = 0;
    while (true) {
        snprintf(buf, sizeof(buf), "Hello (times: %d)", ++times);
        kcp->send(ByteBuffer((uint8_t *)buf, strlen(buf)));
        printf("send -> %s\n", buf);
        sleep(1);
        if (times == 0xff) {
            break;
        }
    }

    return 0;
}
