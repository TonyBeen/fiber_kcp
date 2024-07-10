/*************************************************************************
    > File Name: test_kcp_server.cc
    > Author: hsz
    > Brief:
    > Created Time: Thu 14 Jul 2022 09:34:05 AM CST
 ************************************************************************/

#include <assert.h>
#include <iostream>
#include <signal.h>
#include <log/log.h>
#include <log/callstack.h>

#include "../kcp_manager.h"
#include "../kcp_server.h"

#define LOG_TAG "test-kcp-server"

using namespace std;

#define SERVER_IP   "10.0.24.17"
#define SERVER_PORT 12000

void signalCatch(int sig)
{
    eular::CallStack stack;
    stack.update();
    stack.log(LOG_TAG, eular::LogLevel::LEVEL_FATAL);

    exit(0);
}

int main(int argc, char **argv)
{
    signal(SIGSEGV, signalCatch);
    signal(SIGABRT, signalCatch);

    eular::KcpManager::Ptr pManager(new eular::KcpManager("kcp-server", true));

    eular::KcpServer::SP spServer = std::make_shared<eular::KcpServer>();
    assert(spServer->bind(SERVER_IP, SERVER_PORT));

    eular::ReadEventCB recvEventCB = [](eular::KcpContext::SP spContex, const eular::ByteBuffer &buffer) {
        eular::String8 data = (const char *)buffer.const_data();

        LOGI("[%s:%d] -> [%s:%d]: %s", spContex->getPeerHost().c_str(), spContex->getPeerPort(),
            spContex->getLocalHost().c_str(), spContex->getLocalPort(), data.c_str());

        if (0 == data.strcasecmp("PING")) {
            eular::ByteBuffer bufResponse;
            const char *response = "PONG";
            bufResponse.set((const uint8_t *)response, strlen(response));
            spContex->send(std::move(bufResponse));
        } else {
            spContex->send(buffer);
        }
    };

    spServer->installConnectEvent([recvEventCB](eular::KcpContext::SP spContext) -> bool {
        LOGI("[%s:%d] connected", spContext->getPeerHost().c_str(), spContext->getPeerPort());
        spContext->installRecvEvent(recvEventCB);
        return true;
    });

    spServer->installDisconnectEvent([](eular::KcpContext::SP spContext) {
        LOGI("[%s:%d] disconnected", spContext->getPeerHost().c_str(), spContext->getPeerPort());
    });

    assert(pManager->addKcp(spServer));

    pManager->start();
    return 0;
}
