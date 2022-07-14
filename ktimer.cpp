/*************************************************************************
    > File Name: kcp_timer.cpp
    > Author: hsz
    > Brief:
    > Created Time: Thu 07 Jul 2022 09:15:46 AM CST
 ************************************************************************/

#include "ktimer.h"
#include <log/log.h>
#include <assert.h>
#include <atomic>
#include <chrono>

#define LOG_TAG "KTimer"

std::atomic<uint64_t>   gUniqueIdCount{0};

KTimer::KTimer() :
    mTime(0),
    mRecycleTime(0),
    mCb(nullptr)
{
    mUniqueId = ++gUniqueIdCount;
}

KTimer::KTimer(uint64_t ms, CallBack cb, uint32_t recycle, uint32_t tid) :
    mTid(tid),
    mCb(cb),
    mRecycleTime(recycle)
{
    mTime = CurrentTime() + ms;
    mUniqueId = ++gUniqueIdCount;
}

KTimer::KTimer(const KTimer& other) :
    mTime(other.mTime),
    mCb(other.mCb),
    mRecycleTime(other.mRecycleTime),
    mUniqueId(other.mUniqueId)
{
}

KTimer::~KTimer()
{

}

KTimer &KTimer::operator=(const KTimer& timer)
{
    assert(this != &timer);
    mTime = timer.mTime;
    mCb = timer.mCb;
    mRecycleTime = timer.mRecycleTime;
    mUniqueId = timer.mUniqueId;

    return *this;
}

void KTimer::cancel()
{
    AutoLock<Mutex> lock(mMutex);
    mTime = 0;
    mCb = nullptr;
    mRecycleTime = 0;
}

void KTimer::setCallback(CallBack cb)
{
    AutoLock<Mutex> lock(mMutex);
    mCb = cb;
}

KTimer::CallBack KTimer::getCallback()
{
    AutoLock<Mutex> lock(mMutex);
    return mCb;
}

void KTimer::update()
{
    if (mRecycleTime) {
        mTime += mRecycleTime;
    }
}

void KTimer::reset(uint64_t ms, KTimer::CallBack cb, uint32_t recycle, uint32_t tid)
{
    if (ms < mTime || cb == nullptr) {
        return;
    }

    mTime = CurrentTime() + ms;
    mCb = cb;
    mRecycleTime = recycle;
    mTid = tid;
    onReset();
}

void KTimer::onReset()
{

}

uint64_t KTimer::CurrentTime()
{
    std::chrono::steady_clock::time_point tm = std::chrono::steady_clock::now();
    std::chrono::milliseconds mills = 
        std::chrono::duration_cast<std::chrono::milliseconds>(tm.time_since_epoch());
    return mills.count();
}


KTimerManager::KTimerManager()
{
}

KTimerManager::~KTimerManager()
{
}

uint64_t KTimerManager::getNearTimeout()
{
    RDAutoLock<RWMutex> rlock(mTimerRWMutex);
    mTickle = false;
    if (mTimers.empty()) {
        return UINT64_MAX;
    }

    const KTimer::SP &timer = *mTimers.begin();
    uint64_t nowMs = KTimer::CurrentTime();
    if (nowMs >= timer->getTimeout()) {
        return 0;
    }
    return timer->getTimeout() - nowMs;
}

KTimer::SP KTimerManager::addTimer(uint64_t ms, KTimer::CallBack cb, uint32_t recycle, uint32_t tid)
{
    KTimer::SP timer(new (std::nothrow)KTimer(ms, cb, recycle, tid));
    return addTimer(timer);
}

static void onTimer(std::weak_ptr<void> cond, std::function<void()> cb)
{
    std::shared_ptr<void> temp = cond.lock();
    if (temp) {
        cb();
    }
}

KTimer::SP KTimerManager::addConditionTimer(uint64_t ms, KTimer::CallBack cb, std::weak_ptr<void> cond, uint32_t recycle)
{
    return addTimer(ms, std::bind(&onTimer, cond, cb), recycle);
}

void KTimerManager::delTimer(uint64_t timerId)
{
    WRAutoLock<RWMutex> wrLock(mTimerRWMutex);
    for (auto it = mTimers.begin(); it != mTimers.end();) {
        if ((*it)->mUniqueId == timerId) {
            mTimers.erase(it);
            break;
        }
        ++it;
    }
}

void KTimerManager::listExpiredTimer(std::list<std::pair<std::function<void()>, uint32_t>> &cbs)
{
    uint64_t nowMS = KTimer::CurrentTime();
    std::list<KTimer::SP> expired;
    {
        RDAutoLock<RWMutex> rdlock(mTimerRWMutex);
        if (mTimers.empty()) {
            return;
        }
    }

    WRAutoLock<RWMutex> wrlock(mTimerRWMutex);
    auto it = mTimers.begin();
    while (it != mTimers.end() && (*it)->mTime <= nowMS) {
        ++it;
    }
    expired.insert(expired.begin(), mTimers.begin(), it);
    mTimers.erase(mTimers.begin(), it);

    for (auto &timer : expired) {
        if (timer->mCb != nullptr) {    // 排除用户取消的定时器
            cbs.push_back(std::make_pair(timer->getCallback(), timer->mTid));
            if (timer->mRecycleTime) {
                timer->update();
                mTimers.insert(timer);
            }
        }
    }
}

KTimer::SP KTimerManager::addTimer(KTimer::SP timer)
{
    if (timer == nullptr) {
        return nullptr;
    }

    LOGD("addTimer(%p) %lu", timer.get(), timer->mUniqueId);
    mTimerRWMutex.wlock();
    auto it = mTimers.insert(timer).first;
    bool atFront = (it == mTimers.begin()) && !mTickle;
    if (atFront) {
        mTickle = true;
    }
    mTimerRWMutex.unlock();

    if (atFront) {
        onTimerInsertedAtFront();
    }

    return timer;
}
