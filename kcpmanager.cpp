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

KcpManager::KcpManager(uint8_t threads, const String8 &name) :
    mEventCount(0),
    mEventFd(-1),
    mKeepRun(false)
{
    mScheduler.reset(new (std::nothrow)KScheduler(threads, false, name));
    if (mScheduler == nullptr) {
        LOGE("no memory");
        return;
    }

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
}

KcpManager::~KcpManager()
{
    stop();
    AutoLock<Mutex> lock(mCtxMutex);
    for (auto it : mContextVec) {
        if (it) {
            delete it;
        }
    }
    mContextVec.clear();
    if (mEpollFd > 0) {
        close(mEpollFd);
    }
}

bool KcpManager::addKcp(Kcp::SP kcp)
{
    if (kcp == nullptr) {
        return false;
    }

    if (mEventCount >= epoll_event_size) {
        LOGW("events are full");
        return false;
    }

    epoll_event ev;
    uint32_t tid = 0;
    uint32_t min = mKcpPeerThread.begin()->second;
    for (auto it = mKcpPeerThread.begin(); it != mKcpPeerThread.end(); ++it) {
        if (it->second < min) {
            min = it->second;
            tid = it->first;
        }
    }
    int fd = kcp->mAttr.fd;

    Context *ctx = nullptr;
    {
        AutoLock<Mutex> lock(mCtxMutex);
        if (fd >= mContextVec.size()) {
            contextResize(fd * 1.5);
        }
        ctx = mContextVec[fd];
    }
    if (ctx == nullptr) {
        return false;
    }
    if (ctx->read != nullptr || ctx->write != nullptr) { // 说明已存在了
        return true;
    }

    int flag = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
    ctx->events = READ;
    ctx->fd = fd;
    ctx->read = std::bind(&Kcp::inputRoutine, kcp.get());
    ctx->scheduler = mScheduler.get();
    ctx->tid = tid;
    ctx->timerId = addTimer(kcp->mAttr.interval * 1.5,
        std::bind(&Kcp::outputRoutine, kcp.get()), kcp->mAttr.interval * 1.5, tid)->getUniqueId();
    ev.data.ptr = ctx;
    ev.events = EPOLLET | EPOLLIN;
    int rt = epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &ev);
    if (rt < 0) {
        ctx->resetContext(READ);
        delTimer(ctx->timerId);
    }
    return rt == 0;
}

bool KcpManager::delKcp(Kcp::SP kcp)
{
    if (kcp == nullptr) {
        return false;
    }

    int fd = kcp->mAttr.fd;
    int rt = epoll_ctl(mEpollFd, EPOLL_CTL_DEL, fd, nullptr);
    if (rt < 0) {
        LOGE("epoll_ctl error. [%d,%s]", errno, strerror(errno));
        return false;
    }
    Context *ctx = nullptr;
    {
        AutoLock<Mutex> lock(mCtxMutex);
        ctx = mContextVec[fd];
        LOG_ASSERT2(ctx != nullptr);
        ctx->resetContext(READ);
        ctx->resetContext(WRITE);
    }
    delTimer(ctx->timerId);
    return true;
}

bool KcpManager::start(bool userCaller)
{
    if (mKeepRun) {
        return true;
    }
    mKeepRun = true;
    mScheduler->start();
    auto tidVec = mScheduler->gettids();
    for (auto it : tidVec) {
        mKcpPeerThread.insert(std::move(std::make_pair(it, 0)));
    }
    if (userCaller) {
        threadloop();
        return true;
    }
    mThread.reset(new (std::nothrow)Thread(std::bind(&KcpManager::threadloop, this)));
    return mThread != nullptr;
}

bool KcpManager::stop()
{
    if (mKeepRun == false) {
        return true;
    }

    mKeepRun = false;
    mScheduler->stop();
    return true;
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

void KcpManager::threadloop()
{
    epoll_event *events = new epoll_event[epoll_event_size];
    std::shared_ptr<epoll_event> ptr(events, [](epoll_event *p) {
        if (p) {
            delete[] p;
        }
    });
    LOG_ASSERT2(events != nullptr);

    uint64_t timeoutms = 10;
    while (mKeepRun) {
        timeoutms = getNearTimeout();
        int nev = 0;
        do {
            nev = epoll_wait(mEpollFd, events, epoll_event_size, timeoutms);
            if (nev < 0 && errno == EINTR) {
            } else {
                break;
            }
        } while (true);

        if (nev < 0) {
            LOGE("epoll_wait error. [%d, %s]", errno, strerror(errno));
            break;
        }

        std::list<std::pair<std::function<void()>, uint32_t>> cbs;
        listExpiredTimer(cbs);
        mScheduler->schedule(cbs.begin(), cbs.end());

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
    }
}

void KcpManager::onTimerInsertedAtFront()
{
    eventfd_write(mEventFd, 1);
}

void KcpManager::Context::resetContext(uint32_t event)
{
    switch (event) {
    case READ:
        read = nullptr;
        break;
    case WRITE:
        write = nullptr;
        break;
    default:
        LOG_ASSERT2(false);
        break;
    }
}

void KcpManager::Context::triggerEvent(Event event)
{
    switch (event) {
    case READ:
        scheduler->schedule(read, tid);
        break;
    case WRITE:
        scheduler->schedule(write, tid);
    default:
        break;
    }
}
