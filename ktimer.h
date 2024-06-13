/*************************************************************************
    > File Name: kcp_timer.h
    > Author: hsz
    > Brief:
    > Created Time: Thu 07 Jul 2022 09:15:42 AM CST
 ************************************************************************/

#ifndef __KCP_TIMER_H__
#define __KCP_TIMER_H__

#include <utils/utils.h>
#include <utils/mutex.h>
#include <utils/singleton.h>
#include <sys/epoll.h>
#include <stdint.h>
#include <set>
#include <memory>
#include <list>
#include <functional>

#include "heap_timer.h"

class KTimer final : public std::enable_shared_from_this<KTimer>
{
    friend class KTimerManager;
public:
    typedef std::shared_ptr<KTimer> SP;
    typedef std::weak_ptr<KTimer>   WP;
    typedef std::unique_ptr<KTimer> Ptr;
    typedef std::function<void(void)> CallBack;

    ~KTimer();
    KTimer(const KTimer& timer) = delete;
    KTimer &operator=(const KTimer& timer) = delete;

    uint64_t getUniqueId() const { return m_timerCtx.unique_id; }
    void cancel() { m_canceled = true; }

    static uint64_t CurrentTime();

private:
    KTimer();
    KTimer(uint64_t ms, CallBack cb, uint32_t recycle);

    heap_node_t *getHeapNode() { return &m_timerCtx.node; }
    uint64_t getTimeout() const { return m_timerCtx.next_timeout; }
    void setNextTime(uint64_t timeMs) { m_timerCtx.next_timeout = timeMs; }
    void setRecycleTime(uint64_t ms) { m_timerCtx.recycle_time = ms; }
    void setCallback(CallBack cb) { m_timerCallback = cb; }
    CallBack getCallback() { return m_timerCallback; }
    void update();

private:
    heap_timer_t        m_timerCtx;         // 定时器信息
    std::atomic<bool>   m_canceled;         // 定时器是否取消
    CallBack            m_timerCallback;    // 定时器回调函数
};

class KTimerManager
{
    DISALLOW_COPY_AND_ASSIGN(KTimerManager);
public:
    KTimerManager();
    virtual ~KTimerManager();

    uint64_t    getNearTimeout();
    KTimer::SP  addTimer(uint64_t ms, KTimer::CallBack cb, uint32_t recycle = 0);
    KTimer::SP  addConditionTimer(uint64_t ms, KTimer::CallBack cb, std::weak_ptr<void> cond, uint32_t recycle = 0);
    void        delTimer(uint64_t timerId);

protected:
    void            listExpiredTimer(std::list<KTimer::CallBack> &cbList);
    KTimer::SP      addTimer(KTimer::SP timer);
    virtual void    onTimerInsertedAtFront() = 0;

private:
    eular::RWMutex          m_timerRWMutex; // 保护heap和set
    heap_t                  m_timerHeap;    // 定时器堆
    std::set<KTimer::SP>    m_timerSet;     // 定时器集合
};

#endif  // __KCP_TIMER_H__
