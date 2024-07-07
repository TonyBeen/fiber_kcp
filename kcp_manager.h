/*************************************************************************
    > File Name: kcpmanager.h
    > Author: hsz
    > Brief:
    > Created Time: Mon 04 Jul 2022 02:44:39 PM CST
 ************************************************************************/

#ifndef __KCP_MANAGER_H__
#define __KCP_MANAGER_H__

#include <atomic>
#include <map>
#include <vector>

#include <sys/epoll.h>

#include "kcp.h"
#include "ktimer.h"
#include "kschedule.h"

namespace eular {
class KcpManager : public KTimerManager, public KScheduler
{
public:
    using SP = std::shared_ptr<KcpManager>;
    using Ptr = std::unique_ptr<KcpManager>;

    KcpManager(const String8 &name, bool userCaller);
    virtual ~KcpManager();

    enum Event {
        NONE = 0,
        READ = EPOLLIN,
        WRITE = EPOLLOUT
    };

    bool addKcp(Kcp::SP kcp);
    bool delKcp(Kcp::SP kcp);

    virtual void start() override;
    virtual void stop() override;

    static KcpManager *GetCurrentKcpManager();

private:
    void threadloop();
    virtual void tickle() override;
    virtual void onTimerInsertedAtFront() override;

    enum class KcpState {
        NOTINIT = 0,
        INITED,
        REMOVE,
    };

    struct Context {
        using SP = std::shared_ptr<Context>;
        void resetContext(uint32_t event);
        void triggerEvent(Event event);

        KScheduler *scheduler = nullptr;
        std::function<void()> read = nullptr;
        std::function<void()> write = nullptr;
        uint64_t timerId = 0;
        int32_t socketFd = 0;
        uint32_t events = NONE;
        Mutex mutex;
    };

private:
    using ContxtMap = std::map<int32_t, Context::SP>;
    eular::Mutex            m_ctxMutex;
    ContxtMap               m_contextMap;
    std::atomic<uint16_t>   m_kcpCtxCount;
    std::atomic<bool>       m_keepRun;
    Thread::SP              m_thread;
    int32_t                 m_epollFd;
    int32_t                 m_eventFd;
};

} // namespace eular
#endif  // __KCP_MANAGER_H__
