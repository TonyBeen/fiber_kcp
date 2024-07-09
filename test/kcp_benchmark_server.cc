/*************************************************************************
    > File Name: kcp_benchmark.cc
    > Author: hsz
    > Brief:
    > Created Time: Wed 20 Jul 2022 01:44:12 PM CST
 ************************************************************************/

#include <assert.h>
#include <signal.h>

#include <atomic>

#include <log/log.h>
#include <log/callstack.h>

#include "../kcp_manager.h"
#include "../kcp_server.h"

#define SERVER_IP   "10.0.24.17"
#define SERVER_PORT 12000

#define LOG_TAG     "kcp-benchmark"
#define SERVER_IP   "10.0.24.17"
#define SERVER_PORT 12000

void signalCatch(int sig)
{
    eular::CallStack stack;
    stack.update();
    stack.log(LOG_TAG, eular::LogLevel::LEVEL_FATAL);

    exit(0);
}

static std::atomic<uint64_t> g_receivedSize{0};

int main(int argc, char **argv)
{
    signal(SIGSEGV, signalCatch);
    signal(SIGABRT, signalCatch);

    eular::log::InitLog(eular::LogLevel::LEVEL_WARN);

    eular::KcpManager::Ptr pManager(new eular::KcpManager("kcp-server", true));

    eular::KcpServer::SP spServer = std::make_shared<eular::KcpServer>();
    assert(spServer->bind(SERVER_IP, SERVER_PORT));

    eular::ReadEventCB recvEventCB = [](eular::KcpContext::SP spContex, const eular::ByteBuffer &buffer) {
        g_receivedSize += buffer.size();
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
    pManager->addTimer(1000, [](){
        uint64_t recvSize = g_receivedSize;
        g_receivedSize = 0;

        if (recvSize / 1000 / 1000 > 0) {
            LOGW("onTimerEvent() %d Mb/s", recvSize / 1000 / 1000);
        } else if (recvSize / 1000 > 0) {
            LOGW("onTimerEvent() %d Kb/s", recvSize / 1000);
        } else {
            LOGW("onTimerEvent() %d b/s", recvSize);
        }
    }, 1000);

    pManager->start();
    return 0;
}
