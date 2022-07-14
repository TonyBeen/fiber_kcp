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

class KcpManager : public KTimerManager, public KScheduler
{
public:
    KcpManager(uint8_t threads, bool userCaller, const String8 &name);
    virtual ~KcpManager();

    enum Event {
        NONE = 0,
        READ = EPOLLIN,
        WRITE = EPOLLOUT
    };

    bool addKcp(Kcp::SP kcp);
    bool delKcp(Kcp::SP kcp);

    static KcpManager *GetThis();

private:
    virtual void idle() override;
    virtual void tickle() override;
    virtual void onTimerInsertedAtFront() override;

    enum class KcpState {
        NOTINIT = 0,
        INITED,
        REMOVE,
    };

    struct Context {
        struct EventContext {
            KScheduler *scheduler = nullptr;
            KFiber::SP fiber;
            std::function<void()> cb;
        };

        EventContext& getContext(Event event);

        void resetContext(uint32_t event);
        void triggerEvent(Event event);

        EventContext read;
        EventContext write;
        uint64_t timerId;
        uint32_t tid;
        int fd = 0;
        uint32_t events = NONE;
        Mutex mutex;
    };

    void contextResize(uint32_t size);
    bool stopping(uint64_t &timeout);

private:
    eular::Mutex mQueueMutex;
    std::map<Kcp::SP, KcpState, Kcp::KcpCompare> mWaitingQueue;
    eular::Mutex mCtxMutex;
    std::vector<Context *>  mContextVec;
    std::atomic<uint16_t>   mEventCount;
    int         mEpollFd;
    int         mEventFd;
};

typedef eular::Singleton<KcpManager> KcpManagerInstance;

#endif  // __KCP_MANAGER_H__
