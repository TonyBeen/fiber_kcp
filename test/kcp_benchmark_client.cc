/*************************************************************************
    > File Name: test_kcp_client.cc
    > Author: hsz
    > Brief:
    > Created Time: Thu 14 Jul 2022 10:11:15 AM CST
 ************************************************************************/

#include <assert.h>
#include <signal.h>
#include <stdio.h>

#include <log/log.h>
#include <log/callstack.h>

#include "../kcp_manager.h"
#include "../kcp_client.h"

#define LOG_TAG "kcp-benchmark-client"

using namespace std;

#define SERVER_IP   "10.0.24.17"
#define SERVER_PORT 12000

volatile bool g_exit = false;

void signalCatch(int sig)
{
    if (sig == SIGSEGV) {
        eular::CallStack stack;
        stack.update();
        stack.log(LOG_TAG, eular::LogLevel::LEVEL_FATAL);

        exit(0);
    } else if (sig == SIGINT) {
        g_exit = true;
    }
}

int main(int argc, char **argv)
{
    signal(SIGSEGV, signalCatch);
    signal(SIGABRT, signalCatch);
    signal(SIGINT, signalCatch);

    eular::log::InitLog(eular::LogLevel::LEVEL_INFO);

    eular::ReadEventCB recvEventCB = [](eular::KcpContext::SP spContex, const eular::ByteBuffer &buffer) {
        eular::String8 data = (const char *)buffer.const_data();

        LOGI("[%s:%d] -> [%s:%d]: %s", spContex->getPeerHost().c_str(), spContex->getPeerPort(),
            spContex->getLocalHost().c_str(), spContex->getLocalPort(), data.c_str());
    };

    eular::KcpManager::Ptr pManager(new eular::KcpManager("kcp-benchmark-client", false));

    eular::KcpClient::SP spClient = std::make_shared<eular::KcpClient>();
    spClient->setWindowSize(1024, 1024);
    assert(spClient->bind("0.0.0.0"));
    assert(spClient->connect(SERVER_IP, SERVER_PORT, KCPMode::Fast2));

    eular::KcpContext::SP spClientContext = spClient->getContext();
    assert(spClientContext != nullptr);
    spClientContext->installRecvEvent(recvEventCB);
    spClientContext->setSendBufferSize(128 * 1024);

    spClient->installDisconnectEvent([](eular::KcpContext::SP spContext) {
        LOGI("[%s:%d] disconnected", spContext->getPeerHost().c_str(), spContext->getPeerPort());
    });

    pManager->addKcp(spClient);
    pManager->start();

    // 一包udp数据最大size * 16
    static const uint32_t SIZE = (MTU_SIZE - protocol::KCP_PROTOCOL_SIZE) * 32;
    static char buf[SIZE] = {0};
    while (!g_exit) {
        spClientContext->send(std::move(eular::ByteBuffer(buf, SIZE)));
        msleep(10);
    }

    spClientContext->closeContext();
    sleep(1);
    pManager->stop();
    return 0;
}
