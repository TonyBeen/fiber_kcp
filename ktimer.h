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

using namespace eular;

class KTimer
{
    friend class KTimerManager;
public:
    typedef std::shared_ptr<KTimer> SP;
    typedef std::function<void(void)> CallBack;

    ~KTimer();
    KTimer &operator=(const KTimer& timer);

    uint64_t getTimeout() const { return mTime; }
    uint64_t getUniqueId() const { return mUniqueId; }
    void setNextTime(uint64_t timeMs) { mTime = timeMs; }
    void setCallback(CallBack cb) { mCb = cb; }
    void setRecycleTime(uint64_t ms) { mRecycleTime = ms; }

    void cancel();
    void update();
    void reset(uint64_t ms, CallBack cb, uint32_t recycle, uint32_t tid);

    static uint64_t CurrentTime();

protected:
    KTimer();
    KTimer(uint64_t ms, CallBack cb, uint32_t recycle, uint32_t tid);
    KTimer(const KTimer& timer);

    virtual void onReset();

    struct Comparator {
        bool operator()(const KTimer::SP &left, const KTimer::SP &right) {
            if (left == nullptr && right == nullptr) {
                return false;
            }
            if (left == nullptr) {
                return true;
            }
            if (right == nullptr) {
                return false;
            }
            if (left->mTime == right->mTime) { // 时间相同，比较ID
                return left->mUniqueId < right->mUniqueId;
            }
            return left->mTime < right->mTime;
        }
    };

private:
    uint32_t    mTid;           // 将定时器与线程绑定
    uint64_t    mTime;          // (绝对时间)下一次执行时间(ms)
    uint64_t    mRecycleTime;   // 循环时间ms
    CallBack    mCb;            // 回调函数
    uint64_t    mUniqueId;      // 定时器唯一ID
};


class KTimerManager
{
    DISALLOW_COPY_AND_ASSIGN(KTimerManager);
public:
    typedef std::set<KTimer *, KTimer::Comparator>::iterator KTimerIterator;

    KTimerManager();
    virtual ~KTimerManager();

    uint64_t    getNearTimeout();
    KTimer::SP  addTimer(uint64_t ms, KTimer::CallBack cb, uint32_t recycle = 0, uint32_t tid = 0);
    KTimer::SP  addConditionTimer(uint64_t ms, KTimer::CallBack cb, std::weak_ptr<void> cond, uint32_t recycle = 0);
    void        delTimer(uint64_t timerId);

protected:
    void            listExpiredTimer(std::list<std::pair<std::function<void()>, uint32_t>> &cbs);
    KTimer::SP      addTimer(KTimer::SP timer);
    virtual void    onTimerInsertedAtFront() = 0;

private:
    RWMutex mTimerRWMutex;
    bool mTickle = false;       // 是否触发onTimerInsertedAtFront
    std::set<KTimer::SP, KTimer::Comparator>  mTimers;  // 定时器集合
};

#endif  // __KCP_TIMER_H__
