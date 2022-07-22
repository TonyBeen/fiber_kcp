/*************************************************************************
    > File Name: schedule.h
    > Author: hsz
    > Brief:
    > Created Time: Thu 07 Jul 2022 09:15:01 AM CST
 ************************************************************************/

#ifndef __KCP_SCHEDULE_H__
#define __KCP_SCHEDULE_H__

#include "kfiber.h"
#include "kthread.h"
#include <utils/string8.h>
#include <utils/mutex.h>
#include <memory>
#include <vector>
#include <list>
#include <atomic>

class KScheduler
{
public:
    typedef std::shared_ptr<KScheduler> SP;

    KScheduler(uint8_t threads = 1, bool userCaller = false, const eular::String8 &name = "");
    virtual ~KScheduler();

    void start();
    void stop();

    static KScheduler* GetThis();
    static KFiber* GetMainFiber();
    const eular::String8 &getName() const { return mName; }
    const std::vector<int> &gettids() const { return mThreadIds; }
    bool hasIdleThread() const { return mIdleThreadCount.load() > 0; }

    template<class FiberOrCb>
    void schedule(FiberOrCb fc, int th = 0)
    {
        bool needTickle = false;
        {
            AutoLock<Mutex> lock(mQueueMutex);
            needTickle = scheduleNoLock(fc, th);
        }
        if (needTickle) {
            tickle();
        }
    }

    template<class Iterator>
    void schedule(Iterator begin, Iterator end)
    {
        bool needTickle = false;
        {
            AutoLock<Mutex> lock(mQueueMutex);
            while (begin != end) {
                needTickle = scheduleNoLock(begin->first, begin->second) || needTickle;
                ++begin;
            }
        }
        if (needTickle) {
            tickle();
        }
    }

    void switchTo(int th = -1);

protected:
    void setThis();
    void threadloop();
    /**
     * @brief 唤醒处于idle阻塞态的线程
     */
    virtual void idle();
    virtual void tickle();
    virtual bool stopping();

    struct FiberBindThread {
        KFiber::SP fiberPtr;        // 协程智能指针对象
        std::function<void()> cb;   // 协程执行函数
        int tid;                    // 内核线程ID

        FiberBindThread() : tid(-1) {}
        FiberBindThread(KFiber::SP sp, int th) : fiberPtr(sp), tid(th) {}
        FiberBindThread(KFiber::SP *sp, int th) : tid(th) { fiberPtr.swap(*sp); }
        FiberBindThread(std::function<void()> f, int th) : cb(f), tid(th) {}
        FiberBindThread(std::function<void()> *f, int th) : tid(th) { cb.swap(*f); }

        void reset()
        {
            fiberPtr = nullptr;
            cb = nullptr;
            tid = -1;
        }
    };

    template<class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int tid) {
        bool needTickle = mFiberQueue.empty();
        FiberBindThread ft(fc, tid);
        if(ft.fiberPtr || ft.cb) {
            mFiberQueue.push_back(ft);
        }
        return needTickle;
    }

protected:
    std::vector<Thread::SP> mThreads;           // 线程数组
    std::vector<int>        mThreadIds;         // 线程id数组
    uint32_t                mThreadCount;       // 线程数量
    uint8_t                 mContainUserCaller; // 是否包含用户线程
    int                     mRootThread;        // userCaller为true时，为用户调用线程ID，false为-1
    std::atomic<bool>       mStopping;          // 是否停止

    std::atomic<uint32_t>   mActiveThreadCount = {0};
    std::atomic<uint32_t>   mIdleThreadCount = {0};

private:
    eular::String8          mName;          // 调度器名字
    eular::Mutex            mQueueMutex;    // 任务队列锁
    KFiber::SP              mRootFiber;     // userCaller为true时有效
    std::list<FiberBindThread> mFiberQueue;
};


#endif  // __KCP_SCHEDULE_H__