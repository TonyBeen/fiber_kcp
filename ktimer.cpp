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

int32_t TimerCompare(const heap_node_t *lhs, const heap_node_t *rhs)
{
    heap_timer_t *pTimerLeft = HEAP_NODE_TO_TIMER(lhs);
    heap_timer_t *pTimerRight = HEAP_NODE_TO_TIMER(rhs);

    return pTimerLeft->next_timeout < pTimerRight->next_timeout;
}

KTimer::KTimer() :
    m_timerCallback(nullptr)
{
    m_timerCtx.unique_id = ++gUniqueIdCount;
    m_timerCtx.user_data = this;
}

KTimer::KTimer(uint64_t ms, CallBack cb, uint32_t recycle) :
    m_timerCallback(cb)
{
    m_timerCtx.next_timeout = CurrentTime() + ms;
    m_timerCtx.unique_id = ++gUniqueIdCount;
    m_timerCtx.user_data = this;
}

KTimer::~KTimer()
{
    m_timerCtx.user_data = nullptr;
}

void KTimer::update()
{
    if (m_timerCtx.recycle_time) {
        m_timerCtx.next_timeout += m_timerCtx.recycle_time;
    }
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
    heap_init(&m_timerHeap, TimerCompare);
}

KTimerManager::~KTimerManager()
{
    heap_init(&m_timerHeap, TimerCompare);
    m_timerSet.clear();
}

uint64_t KTimerManager::getNearTimeout()
{
    uint64_t nowMs = KTimer::CurrentTime();

    eular::RDAutoLock<eular::RWMutex> readLock(m_timerRWMutex);
    if (m_timerHeap.root == nullptr) {
        return UINT64_MAX;
    }

    heap_timer_t *pTimer = HEAP_NODE_TO_TIMER(m_timerHeap.root);
    if (nowMs >= pTimer->next_timeout) {
        return 0;
    }

    return pTimer->next_timeout - nowMs;
}

KTimer::SP KTimerManager::addTimer(uint64_t ms, KTimer::CallBack cb, uint32_t recycle)
{
    KTimer::SP timer = std::make_shared<KTimer>(ms, cb, recycle);
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
    bool isRootNode = false;
    {
        eular::WRAutoLock<eular::RWMutex> writeLock(m_timerRWMutex);
        for (auto it = m_timerSet.begin(); it != m_timerSet.end(); ++it) {
            if ((*it)->getUniqueId() == timerId) {
                // heap_remove()
                heap_node_t *pNode = &(*it)->m_timerCtx.node;
                if (pNode == m_timerHeap.root) {
                    isRootNode = true;
                }
                heap_remove(&m_timerHeap, pNode);
                m_timerSet.erase(it);
                break;
            }
        }
    }

    if (isRootNode) {
        onTimerInsertedAtFront();
    }
}

void KTimerManager::listExpiredTimer(std::list<KTimer::CallBack> &cbList)
{
    uint64_t nowMS = KTimer::CurrentTime();
    eular::RDAutoLock<eular::RWMutex> readLcok(m_timerRWMutex);

    heap_timer_t *pTimer = nullptr;
    while (m_timerHeap.root != nullptr) {
        pTimer = HEAP_NODE_TO_TIMER(m_timerHeap.root);
        if (pTimer->next_timeout > nowMS) {
            break;
        }

        heap_dequeue(&m_timerHeap);
        KTimer *pKTimer = static_cast<KTimer *>(pTimer->user_data);
        cbList.push_back(pKTimer->m_timerCallback);
        if (pTimer->recycle_time > 0) {
            pKTimer->update();
            heap_insert(&m_timerHeap, pKTimer->getHeapNode());
        } else {
            // 从集合中移除
            m_timerSet.erase(pKTimer->shared_from_this());
        }
    }
}

KTimer::SP KTimerManager::addTimer(KTimer::SP timer)
{
    if (timer == nullptr) {
        return nullptr;
    }

    bool atFront = false;
    LOGD("addTimer(%p) %lu", timer.get(), timer->getUniqueId());
    {
        eular::WRAutoLock<eular::RWMutex> writeLock(m_timerRWMutex);
        m_timerSet.insert(timer);
        heap_insert(&m_timerHeap, timer->getHeapNode());
        atFront = (m_timerHeap.root == timer->getHeapNode());
    }

    if (atFront) {
        onTimerInsertedAtFront();
    }

    return timer;
}
