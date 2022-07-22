/*************************************************************************
    > File Name: kcpmanager.h
    > Author: hsz
    > Brief:
    > Created Time: Mon 04 Jul 2022 02:44:39 PM CST
 ************************************************************************/

#ifndef __KCP_MANAGER_H__
#define __KCP_MANAGER_H__

#include "kcp.h"
#include "ktimer.h"
#include "kschedule.h"
#include <utils/singleton.h>
#include <thread>
#include <atomic>
#include <map>
#include <vector>

using namespace eular;

class KcpManager : public KTimerManager
{
public:
    KcpManager(uint8_t threads, const String8 &name);
    virtual ~KcpManager();

    enum Event {
        NONE = 0,
        READ = EPOLLIN,
        WRITE = EPOLLOUT
    };

    bool addKcp(Kcp::SP kcp);
    bool delKcp(Kcp::SP kcp);

    bool start(bool userCaller = false);
    bool stop();

private:
    void threadloop();
    virtual void onTimerInsertedAtFront() override;

    enum class KcpState {
        NOTINIT = 0,
        INITED,
        REMOVE,
    };

    struct Context {
        void resetContext(uint32_t event);
        void triggerEvent(Event event);

        KScheduler *scheduler = nullptr;
        std::function<void()> read = nullptr;
        std::function<void()> write = nullptr;
        uint64_t timerId = 0;
        uint32_t tid = 0;
        int fd = 0;
        uint32_t events = NONE;
        Mutex mutex;
    };

    void contextResize(uint32_t size);

private:
    std::map<int, std::atomic<uint32_t>> mKcpPeerThread;
    eular::Mutex mCtxMutex;
    std::vector<Context *>  mContextVec;
    std::atomic<uint16_t>   mEventCount;
    std::atomic<bool>       mKeepRun;
    KScheduler::SP          mScheduler;
    Thread::SP  mThread;
    int         mEpollFd;
    int         mEventFd;
};

typedef eular::Singleton<KcpManager> KcpManagerInstance;

#endif  // __KCP_MANAGER_H__
