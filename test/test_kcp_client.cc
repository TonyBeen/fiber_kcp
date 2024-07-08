/*************************************************************************
    > File Name: test_kcp_client.cc
    > Author: hsz
    > Brief:
    > Created Time: Thu 14 Jul 2022 10:11:15 AM CST
 ************************************************************************/

#include <assert.h>
#include <iostream>
#include <signal.h>
#include <stdio.h>
#include <log/log.h>
#include <log/callstack.h>

#include "../kcp_manager.h"
#include "../kcp_client.h"

#define LOG_TAG "test-kcp-client"

using namespace std;

#define SERVER_IP   "10.0.24.17"
#define SERVER_PORT 12000

void signalCatch(int sig)
{
    if (sig == SIGSEGV) {
        eular::CallStack stack;
        stack.update();
        stack.log(LOG_TAG, eular::LogLevel::LEVEL_FATAL);
    }

    exit(0);
}

int main(int argc, char **argv)
{
    signal(SIGSEGV, signalCatch);
    signal(SIGABRT, signalCatch);

    eular::KcpManager::Ptr pManager(new eular::KcpManager("kcp-client", false));

    eular::KcpClient::SP spClient = std::make_shared<eular::KcpClient>();
    spClient->setWindowSize(1024, 1024);
    assert(spClient->bind("0.0.0.0"));
    assert(spClient->connect(SERVER_IP, SERVER_PORT));

    eular::KcpContext::SP spClientContext = spClient->getContext();
    assert(spClientContext != nullptr);

    eular::ReadEventCB recvEventCB = [](eular::KcpContext::SP spContex, const eular::ByteBuffer &buffer) {
        eular::String8 data = (const char *)buffer.const_data();

        LOGI("[%s:%d] -> [%s:%d]: %s", spContex->getPeerHost().c_str(), spContex->getPeerPort(),
            spContex->getLocalHost().c_str(), spContex->getLocalPort(),
            data.c_str());
    };

    spClient->installDisconnectEvent([](eular::KcpContext::SP spContext) {
        LOGI("[%s:%d] disconnected", spContext->getPeerHost().c_str(), spContext->getPeerPort());
    });

    pManager->addKcp(spClient);
    pManager->start();

    LOGI("start send...");
    uint8_t buf[4] = {'P', 'I', 'N', 'G' };
    while (true) {
        spClientContext->send(eular::ByteBuffer(buf, sizeof(buf)));
        msleep(50);
    }

    return 0;
}
