/*************************************************************************
    > File Name: schedule.cpp
    > Author: hsz
    > Brief:
    > Created Time: Thu 07 Jul 2022 09:15:05 AM CST
 ************************************************************************/

#include "kschedule.h"
#include <utils/utils.h>
#include <log/log.h>

#define LOG_TAG "KScheduler"

static thread_local KScheduler *gScheduler = nullptr;    // 线程调度器
static thread_local KFiber *gMainFiber = nullptr;        // 调度器的主协程

KScheduler::KScheduler(uint8_t threads, bool userCaller, const eular::String8 &name) :
    mStopping(true),
    mContainUserCaller(userCaller),
    mName(name)
{
    LOGD("%s() start tid num: %d, name: %s", __func__, threads, name.c_str());
    LOG_ASSERT(threads > 0, "%s %s:%s() Invalid Param", __FILE__, __LINE__, __func__);
    if (userCaller) {
        KFiber::GetThis();
        --threads;

        gScheduler = this;
        mRootFiber.reset(new KFiber(std::bind(&KScheduler::threadloop, this)));
        Thread::SetName(name);

        LOGD("root tid id %ld", gettid());
        mThreadIds.push_back(gettid());
        gMainFiber = mRootFiber.get();
        mRootThread = gettid();
    } else {
        mRootThread = -1;
    }

    mThreadCount = threads;
}

KScheduler::~KScheduler()
{
    LOG_ASSERT(mStopping, "You should call stop before deconstruction");
    if (gScheduler == this) {
        gScheduler = nullptr;
    }
}

void KScheduler::setThis()
{
    gScheduler = this;
}

KScheduler* KScheduler::GetThis()
{
    return gScheduler;
}

KFiber* KScheduler::GetMainFiber()
{
    return gMainFiber;
}

void KScheduler::start()
{
    if (mStopping) {
        LOG_ASSERT(mThreads.empty(), "should be empty before start()");
        mStopping = false;
        for (uint8_t i = 0; i < mThreadCount; ++i) {
            Thread::SP ptr(new (std::nothrow)Thread(std::bind(&KScheduler::threadloop, this),
                mName + "_" + std::to_string(i).c_str()));
            mThreads.push_back(ptr);
            mThreadIds.push_back(ptr->getTid());
            ptr->detach();
            LOGD("tid [%s:%d] start", ptr->getName().c_str(), ptr->getTid());
        }
    }
}

void KScheduler::stop()
{
    if (mRootFiber && mThreadCount == 0 && 
        mRootFiber->getState() == KFiber::TERM) {
        // 当只有用户调用线程时且处于结束态
        if (stopping()) {
            return;
        }
    }

    if (mRootThread != -1) {
        LOG_ASSERT2(GetThis() == this);
    } else {
        LOG_ASSERT2(GetThis() != this);
    }

    mStopping = true;
    for (size_t i = 0; i < mThreadCount; ++i) {
        tickle();
    }

    if (mRootFiber) {
        tickle();
    }

    // 用调用线程处理剩余任务
    if (mRootFiber && !stopping()) {
        mRootFiber->call();
    }
}

// 将当前协程切换到th线程
void KScheduler::switchTo(int th)
{
    LOG_ASSERT(KScheduler::GetThis() != nullptr, "");
    if (KScheduler::GetThis() == this) {
        if (th == -1 || th == gettid()) {
            return;
        }
    }

    schedule(KFiber::GetThis(), th);
    KFiber::Yeild2Hold();
}

void KScheduler::threadloop()
{
    LOGD("KScheduler::threadloop() in %s:%p", Thread::GetName().c_str(), Thread::GetThis());
    setThis();
    if (gettid() != mRootThread) {
        gMainFiber = KFiber::GetThis().get();   // 为每个线程创建主协程
    }
    KFiber::SP idleFiber(new KFiber(std::bind(&KScheduler::idle, this)));
    KFiber::SP cbFiber(nullptr);

    FiberBindThread ft;
    while (true) {
        ft.reset();
        bool needTickle = false;
        bool isActive = false;
        {
            AutoLock<Mutex> lock(mQueueMutex);
            auto it = mFiberQueue.begin();
            while (it != mFiberQueue.end()) {
                if (it->tid != 0 && it->tid != gettid()) {   // 不满足线程ID一致的条件
                    ++it;
                    needTickle = true;
                    continue;
                }

                LOG_ASSERT(it->fiberPtr || it->cb, "task can not be null");
                if (it->fiberPtr && it->fiberPtr->getState() == KFiber::EXEC) {  // 找到的协程处于执行状态
                    ++it;
                    continue;
                }

                // LOGD("Find executable tasks");
                ft = *it;
                mFiberQueue.erase(it++);
                ++mActiveThreadCount;
                isActive = true;
                break;
            }
            needTickle |= it != mFiberQueue.end();
        }

        if (needTickle) {
            tickle();
        }

        if (ft.fiberPtr && (ft.fiberPtr->getState() != KFiber::EXEC && ft.fiberPtr->getState() != KFiber::EXCEPT)) {
            ft.fiberPtr->resume();
            --mActiveThreadCount;
            if (ft.fiberPtr->getState() == KFiber::READY) {  // 用户主动设置协程状态为REDAY
                schedule(ft.fiberPtr);
            } else if (ft.fiberPtr->getState() != KFiber::TERM &&
                       ft.fiberPtr->getState() != KFiber::EXCEPT) {
                ft.fiberPtr->mState = KFiber::HOLD;
            }
            ft.reset();
        } else if (ft.cb) {
            if (cbFiber) {
                cbFiber->reset(ft.cb);
            } else {
                cbFiber.reset(new KFiber(ft.cb));
                LOG_ASSERT(cbFiber != nullptr, "");
            }
            ft.reset();
            cbFiber->resume();
            --mActiveThreadCount;
            if (cbFiber->getState() == KFiber::READY) {  // 用户在callback函数内部调用Yeild2Ready
                schedule(cbFiber);
                cbFiber.reset();
            } else if (cbFiber->getState() == KFiber::EXCEPT ||
                    cbFiber->getState() == KFiber::TERM) {
                cbFiber->reset(nullptr);
            } else {    // 用户主动让出协程，需要将当前协程状态设为暂停态HOLD
                cbFiber->mState = KFiber::HOLD;
                cbFiber.reset();
            }
        } else {
            if (isActive) {
                --mActiveThreadCount;
                continue;
            }

            if (idleFiber->getState() == KFiber::TERM) {
                LOGI("idle fiber term");
                break;
            }

            ++mIdleThreadCount;
            idleFiber->resume();
            --mIdleThreadCount;
            if (idleFiber->getState() != KFiber::TERM && 
                idleFiber->getState() != KFiber::EXCEPT) {
                idleFiber->mState = KFiber::HOLD;
            }
        }
    }
}

void KScheduler::idle()
{
    while (!stopping()) {
        LOGI("%s() fiber id: %lu", __func__, KFiber::GetFiberID());
        KFiber::Yeild2Hold();
    }
}

void KScheduler::tickle()
{
    LOGD("KScheduler::tickle()");
}

bool KScheduler::stopping()
{
    AutoLock<Mutex> lock(mQueueMutex);
    return mStopping && (mFiberQueue.size()== 0) && (mActiveThreadCount == 0);
}
