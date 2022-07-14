/*************************************************************************
    > File Name: kcpmanager.cpp
    > Author: hsz
    > Brief:
    > Created Time: Mon 04 Jul 2022 02:44:42 PM CST
 ************************************************************************/

#include "kcpmanager.h"
#include <utils/utils.h>
#include <log/log.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <string.h>
#include <functional>

#define LOG_TAG "KcpManager"

using namespace eular;

static uint32_t epoll_event_size = 1024;

KcpManager::KcpManager(uint8_t threads, bool userCaller, const String8 &name) :
    KScheduler(threads, userCaller, name),
    mEventCount(0),
    mEventFd(-1)
{
    mEpollFd = epoll_create(epoll_event_size);
    if (mEpollFd < 0) {
        LOGE("epoll_create error. [%d, %s]", errno, strerror(errno));
        return;
    }
    mEventFd = eventfd(0, EFD_NONBLOCK);
    if (mEventFd < 0) {
        LOGE("eventfd error. [%d, %s]", errno, strerror(errno));
        return;
    }

    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = mEventFd;

    int ret = epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mEventFd, &ev);
    if (ret < 0) {
        LOGE("epoll_ctl error. [%d, %s]", errno, strerror(errno));
    }
    start();
}

KcpManager::~KcpManager()
{
    stop();
}

bool KcpManager::addKcp(Kcp::SP kcp)
{
    if (mEventCount >= epoll_event_size) {
        LOGW("events are full");
        return false;
    }
    AutoLock<Mutex> lock(mQueueMutex);
    if (kcp == nullptr) {
        return false;
    }
    auto it = mWaitingQueue.insert(std::make_pair(kcp, KcpState::NOTINIT));
    return it.second;
}

bool KcpManager::delKcp(Kcp::SP kcp)
{
    AutoLock<Mutex> lock(mQueueMutex);
    auto it = mWaitingQueue.find(kcp);
    if (it != mWaitingQueue.end()) {
        LOGD("kcp map erase fd %d", it->first->mAttr.fd);
        it->second = KcpState::REMOVE;
    }
    return true;
}

KcpManager *KcpManager::GetThis()
{
    return static_cast<KcpManager *>(KScheduler::GetThis());
}

void KcpManager::idle()
{
    uint32_t maxEvents = epoll_event_size / (mThreadCount ? mThreadCount : 1) + 1;
    epoll_event *events = new epoll_event[maxEvents]();
    std::shared_ptr<epoll_event> ptr(events, [](epoll_event *p) {
        if (p) {
            delete[] p;
        }
    });

    uint32_t localEventCount = 0;
    uint32_t tid = gettid();
    uint64_t timeoutms = 10;
    while (true) {
        {
            // 将等待队列中的kcp加入epoll
            AutoLock<Mutex> lock(mQueueMutex);
            // 负载均衡, 将kcp绑定线程, 否则可能会导致大部分kcp跑在某一个线程，而其他线程没啥任务
            if ((localEventCount + mWaitingQueue.size()) < maxEvents) {
                for (auto it = mWaitingQueue.begin(); it != mWaitingQueue.end(); ++it) {
                    switch (it->second) {
                    case KcpState::NOTINIT:
                    {
                        if (mEventCount >= epoll_event_size) {   // 不能超出事件大小
                            break;
                        }
                        if (it->first->create() == false) {
                            LOGE("create error");
                            break;
                        }

                        epoll_event ev;
                        int fd = it->first->mAttr.fd;
                        int flag = fcntl(fd, F_GETFL);
                        fcntl(fd, F_SETFL, flag | O_NONBLOCK);
                        ev.data.ptr = it->first.get();
                        ev.events = EPOLLET | EPOLLIN;
                        int ret = epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &ev);
                        if (ret < 0) {
                            LOGE("epoll_ctl(%d, EPOLL_CTL_ADD , %d) error. [%d, %s]", mEpollFd, fd, errno, strerror(errno));
                        } else {
                            ++mEventCount;
                            AutoLock<Mutex> lock(mCtxMutex);
                            if (fd >= mContextVec.size()) {
                                contextResize(fd * 1.5);
                            }

                            Context *ctx = new Context;
                            ctx->events = READ;
                            ctx->fd = fd;
                            ctx->tid = tid;
                            ctx->read.cb = std::bind(&Kcp::inputRoutine, it->first.get());
                            ctx->read.fiber = nullptr;
                            ctx->read.scheduler = KScheduler::GetThis();
                            auto timer = addTimer(it->first->mAttr.interval,
                                std::bind(&Kcp::outputRoutine, it->first.get()),
                                it->first->mAttr.interval, tid);
                            LOG_ASSERT2(timer != nullptr);
                            ctx->timerId = timer->getUniqueId();
                            LOGD("addTimer() timer id: %lu, interval: %d", timer->getUniqueId(), it->first->mAttr.interval);
                            mContextVec[fd] = ctx;
                        }
                        break;
                    }
                    case KcpState::INITED:
                    {
                        break;
                    }
                    case KcpState::REMOVE:
                    {
                        epoll_ctl(mEpollFd, EPOLL_CTL_DEL, it->first->mAttr.fd, nullptr);
                        AutoLock<Mutex> lock(mCtxMutex);
                        delTimer(mContextVec[it->first->mAttr.fd]->timerId);
                        mContextVec[it->first->mAttr.fd]->resetContext(READ);
                        --mEventCount;
                        break;
                    }
                    default:
                        LOG_ASSERT(false, "invalid kcp state");
                        break;
                    }
                }
                localEventCount += mWaitingQueue.size();
                mWaitingQueue.clear();
            }
        }

        if (eular_unlikely(stopping(timeoutms))) {
            break;
        }

        LOGD("%s() timeoutms %lu\n", __func__, timeoutms);
        int nev = 0;
        do {
            nev = epoll_wait(mEpollFd, events, maxEvents, timeoutms);
            if (nev < 0 && errno == EINTR) {
            } else {
                break;
            }
        } while (true);

        if (nev < 0) {
            LOGE("epoll_wait error. [%d, %s]", errno, strerror(errno));
            break;
        }

        // 回调 -- tid
        std::list<std::pair<std::function<void()>, uint32_t>> cbs;
        listExpiredTimer(cbs);
        schedule(cbs.begin(), cbs.end());

        for (int i = 0; i < nev; ++i) {
            epoll_event &ev = events[i];
            if (ev.data.fd == mEventFd) {
                eventfd_t value;
                eventfd_read(mEventFd, &value);
                continue;
            }

            Context *ctx = static_cast<Context *>(ev.data.ptr);
            LOG_ASSERT2(ctx != nullptr);
            AutoLock<Mutex> lock(ctx->mutex);
            if (ev.events & (EPOLLERR | EPOLLHUP)) {
                ev.events |= (EPOLLIN | EPOLLOUT) & ctx->events;
            }
            if (ev.events | EPOLLIN) {
                ctx->triggerEvent(READ);
            }
            if (ev.events | EPOLLOUT) {
                ctx->triggerEvent(WRITE);
            }
        }

        KFiber::Yeild2Hold();
    }
}

void KcpManager::tickle()
{
    if (!hasIdleThread()) {
        return;
    }
    eventfd_write(mEventFd, 1);
}

bool KcpManager::stopping(uint64_t &timeout)
{
    timeout = getNearTimeout();
    return timeout == UINT64_MAX && KScheduler::stopping();
}

void KcpManager::contextResize(uint32_t size)
{
    if (size == mContextVec.size()) {
        return;
    }
    if (size < mContextVec.size()) {
        for (uint32_t i = size; i < mContextVec.size(); ++i) {
            if (mContextVec[i] != nullptr) {
                delete mContextVec[i];
                mContextVec[i] = nullptr;
            }
        }
    }
    mContextVec.resize(size);
    uint32_t i = 0;
    for (auto &it : mContextVec) {
        if (it == nullptr) {
            it = new Context;
            it->fd = i++;
        }
    }
}

void KcpManager::onTimerInsertedAtFront()
{
    tickle();
}

KcpManager::Context::EventContext& KcpManager::Context::getContext(Event event)
{
    switch (event) {
    case READ:
        return read;
    case WRITE:
        return write;
    default:
        LOG_ASSERT2(false);
        break;
    }
}

void KcpManager::Context::resetContext(uint32_t event)
{
    switch (event) {
    case READ:
        read.cb = nullptr;
        read.fiber.reset();
        read.scheduler = nullptr;
        break;
    case WRITE:
        write.cb = nullptr;
        write.fiber.reset();
        write.scheduler = nullptr;
        break;
    default:
        LOG_ASSERT2(false);
        break;
    }
}

void KcpManager::Context::triggerEvent(Event event)
{
    EventContext &eventCtx = getContext(event);
    if (eventCtx.cb) {
        eventCtx.scheduler->schedule(&eventCtx.cb, tid);
    } else {
        eventCtx.scheduler->schedule(&eventCtx.fiber, tid);
    }
}
