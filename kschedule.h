/*************************************************************************
    > File Name: schedule.h
    > Author: hsz
    > Brief:
    > Created Time: Thu 07 Jul 2022 09:15:01 AM CST
 ************************************************************************/

#ifndef __KCP_SCHEDULE_H__
#define __KCP_SCHEDULE_H__

#include <memory>
#include <list>
#include <atomic>

#include <utils/string8.h>
#include <utils/mutex.h>

#include "kfiber.h"
#include "kthread.h"

namespace eular {
class KScheduler
{
public:
    typedef std::shared_ptr<KScheduler> SP;

    KScheduler(const eular::String8 &name = "", bool userCaller = false);
    virtual ~KScheduler();

    virtual void start() = 0;
    virtual void stop();

    static KScheduler* GetThis();
    static KFiber* GetMainFiber();
    const eular::String8 &getName() const { return m_name; }

    template<class FiberOrCb>
    void schedule(FiberOrCb fc)
    {
        {
            AutoLock<Mutex> lock(m_queueMutex);
            scheduleNoLock(fc);
        }
        tickle();
    }

    template<class Iterator>
    void schedule(Iterator begin, Iterator end)
    {
        AutoLock<Mutex> lock(m_queueMutex);
        while (begin != end) {
            scheduleNoLock(*begin);
            ++begin;
        }
    }

protected:
    uint32_t getQueueSize();
    void setThis();
    void processEvnet();
    /**
     * @brief 唤醒处于idle阻塞态的线程
     */
    virtual void idle();
    virtual void tickle();
    virtual bool stopping();

    struct FiberBindThread {
        KFiber::SP fiberPtr;        // 协程智能指针对象
        std::function<void()> cb;   // 协程执行函数

        FiberBindThread() {}
        FiberBindThread(KFiber::SP sp) : fiberPtr(sp) {}
        FiberBindThread(KFiber::SP *sp) { fiberPtr.swap(*sp); }
        FiberBindThread(std::function<void()> f) : cb(f){}
        FiberBindThread(std::function<void()> *f) { cb.swap(*f); }

        void reset()
        {
            fiberPtr.reset();
            cb = nullptr;
        }
    };

    template<class FiberOrCb>
    void scheduleNoLock(FiberOrCb fc) {
        FiberBindThread ft(fc);
        if (ft.fiberPtr || ft.cb) {
            m_fiberQueue.push_back(ft);
        }
    }

protected:
    bool                m_userCaller;   // 是否包含用户线程
    int32_t             m_rootThread;   // userCaller为true时，为用户调用线程ID，false为-1
    std::atomic<bool>   m_stopping;     // 是否停止
    String8             m_name;         // 调度器名字
    KFiber::SP          m_rootFiber;    // userCaller为true时有效
    Mutex               m_queueMutex;   // 队列锁
    std::list<FiberBindThread>  m_fiberQueue;
};

} // namespace eular
#endif  // __KCP_SCHEDULE_H__