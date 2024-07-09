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

namespace eular {

static thread_local KScheduler *gScheduler = nullptr;    // 线程调度器
static thread_local KFiber *gMainFiber = nullptr;        // 调度器的主协程

KScheduler::KScheduler(const eular::String8 &name, bool userCaller) :
    m_userCaller(userCaller),
    m_keepRun(false),
    m_name(name)
{
    LOGD("%s(%s)", __func__, name.c_str());

    if (userCaller) {
        KFiber::GetThis();

        gScheduler = this;
        m_rootFiber = std::make_shared<KFiber>(std::bind(&KScheduler::processEvnet, this));
        Thread::SetThreadName(name);

        LOGD("root tid %ld", gettid());
        gMainFiber = m_rootFiber.get();
        m_rootThread = gettid();
    } else {
        m_rootThread = -1;
    }
}

KScheduler::~KScheduler()
{
    LOG_ASSERT(m_keepRun == false, "You should call stop before deconstruction");
    if (gScheduler == this) {
        gScheduler = nullptr;
    }
}

uint32_t KScheduler::getQueueSize()
{
    AutoLock<Mutex> lock(m_queueMutex);
    return static_cast<uint32_t>(m_fiberQueue.size());
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

void KScheduler::stop()
{
    if (m_rootFiber && m_rootFiber->getState() == KFiber::TERM) {
        // 当只有用户调用线程时且处于结束态
        if (stopping()) {
            return;
        }
    }

    m_keepRun = false;

    if (m_userCaller) {
        LOG_ASSERT2(GetThis() == this);
    } else {
        LOG_ASSERT2(GetThis() != this);
        tickle();
    }

    // 用调用线程处理剩余任务
    if (m_rootThread > 0 && m_rootFiber && !stopping()) {
        m_rootFiber->call();
    }
}

void KScheduler::threadEntry()
{
    try
    {
        setThis();
        KFiber::GetThis(); // 为每个线程创建主协程
        m_rootFiber = std::make_shared<KFiber>(std::bind(&KScheduler::processEvnet, this));
        gMainFiber = m_rootFiber.get();
        gMainFiber->resume();
    }
    catch(const std::exception& e)
    {
        LOGE("%s catch exception: %s", __PRETTY_FUNCTION__, e.what());
    }
}

void KScheduler::processEvnet()
{
    LOGD("KScheduler::processEvnet() in %s: %d", Thread::GetThreadName().c_str(), gettid());
    KFiber::SP idleFiber = std::make_shared<KFiber>(std::bind(&KScheduler::idle, this));
    KFiber::SP cbFiber(nullptr);

    FiberBindThread ft;
    while (m_keepRun) {
        ft.reset();
        bool isActive = false;
        {
            AutoLock<Mutex> lock(m_queueMutex);
            auto it = m_fiberQueue.begin();
            if (it != m_fiberQueue.end()) {
                LOG_ASSERT(it->fiberPtr || it->cb, "task can not be null");

                ft = *it;
                m_fiberQueue.erase(it);
                isActive = true;
            }
        }

        if (ft.fiberPtr && (ft.fiberPtr->getState() != KFiber::EXEC && ft.fiberPtr->getState() != KFiber::EXCEPT)) {
            ft.fiberPtr->resume();
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
                continue;
            }

            if (idleFiber->getState() == KFiber::TERM) {
                LOGI("idle fiber term");
                break;
            }

            idleFiber->resume();
            if (idleFiber->getState() != KFiber::TERM && 
                idleFiber->getState() != KFiber::EXCEPT) {
                idleFiber->mState = KFiber::HOLD;
            }
        }
    }

    // 将idle执行完毕, 平稳退出
    idleFiber->resume();
}

void KScheduler::idle()
{
    while (!stopping()) {
        KFiber::Yeild2Hold();
    }
}

void KScheduler::tickle()
{
}

bool KScheduler::stopping()
{
    return m_keepRun && m_fiberQueue.empty();
}

} // namespace eular
