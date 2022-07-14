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

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 12000

int createSocket()
{
    int server_fd, ret;
    sockaddr_in addr;
    socklen_t len = sizeof(sockaddr_in);

    server_fd = socket(AF_INET, SOCK_DGRAM, 0); // AF_INET:IPV4;SOCK_DGRAM:UDP
    if(server_fd < 0) {
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

    bool reuse = true;
    assert(0 == setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));

    return server_fd;
}

void signalCatch(int sig)
{
    if (sig == SIGSEGV) {
        CallStack stack;
        stack.update();
        stack.log(LOG_TAG, eular::LogLevel::FATAL);
    }
}

int main(int argc, char **argv)
{
    KcpManager *manager = KcpManagerInstance::get(1, true, "test_kcp");

    int udp = createSocket();
    assert(udp > 0);
    sockaddr_in addr;
    socklen_t len = sizeof(addr);

    do {
        char buf[64] = {0};
        int nrecv = ::recvfrom(udp, buf, sizeof(buf), 0, (sockaddr *)&addr, &len);
        if (nrecv < 0) {
            perror("recvfrom error: ");
            return 0;
        }

        if (strcasecmp(buf, "CONNECT") == 0) {
            printf("recv a client[%s:%d]\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            break;
        }
    } while (true);

    uint16_t conv = 0xffff;
    sendto(udp, &conv, sizeof(conv), 0, (sockaddr *)&addr, len);

    KcpAttr attr;
    attr.fd = udp;
    attr.autoClose = true;
    attr.conv = 0xffff;
    attr.interval = 50;

    Kcp::SP kcp(new Kcp(attr));

    manager->addKcp(kcp);
    KcpManager::GetMainFiber()->resume();
    return 0;
}
