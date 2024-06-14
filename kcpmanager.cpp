/*************************************************************************
    > File Name: kcpmanager.cpp
    > Author: hsz
    > Brief:
    > Created Time: Mon 04 Jul 2022 02:44:42 PM CST
 ************************************************************************/

#include "kcpmanager.h"

#include <errno.h>
#include <string.h>
#include <functional>

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <utils/utils.h>
#include <log/log.h>

#define LOG_TAG "KcpManager"

static const uint32_t EPOLL_EVENT_SIZE = 1024;

namespace eular {

static thread_local KcpManager *g_pKcpManager = nullptr;

KcpManager::KcpManager(const String8 &name, bool userCaller) :
    KScheduler(name, userCaller),
    m_kcpCtxCount(0),
    m_eventFd(-1),
    m_keepRun(false)
{
    m_epollFd = epoll_create(EPOLL_EVENT_SIZE);
    if (m_epollFd < 0) {
        LOGE("epoll_create error. [%d, %s]", errno, strerror(errno));
        throw eular::Exception("epoll_create error");
    }
    m_eventFd = eventfd(0, EFD_NONBLOCK);
    if (m_eventFd < 0) {
        LOGE("eventfd error. [%d, %s]", errno, strerror(errno));
        throw eular::Exception("eventfd error");
    }

    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = m_eventFd;

    int32_t ret = epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_eventFd, &ev);
    if (ret < 0) {
        LOGE("epoll_ctl error. [%d, %s]", errno, strerror(errno));
        throw eular::Exception("epoll_ctl error");
    }
}

KcpManager::~KcpManager()
{
    stop();
    m_contextMap.clear();

    if (m_epollFd > 0) {
        close(m_epollFd);
    }
}

bool KcpManager::addKcp(Kcp::SP kcp)
{
    if (kcp == nullptr) {
        return false;
    }

    if (m_kcpCtxCount >= EPOLL_EVENT_SIZE) {
        LOGW("events are full");
        return false;
    }

    epoll_event ev;
    int32_t sockFd = kcp->m_updSocket;
    Context::SP ctx = nullptr;
    {
        AutoLock<Mutex> lock(m_ctxMutex);
        ctx = m_contextMap[sockFd];
    }
    if (ctx == nullptr) {
        return false;
    }
    if (ctx->read != nullptr || ctx->write != nullptr) { // 说明已存在了
        return true;
    }

    kcp->m_pKcpManager = this;

    int32_t flag = fcntl(sockFd, F_GETFL);
    fcntl(sockFd, F_SETFL, flag | O_NONBLOCK);
    ctx->events = READ;
    ctx->socketFd = sockFd;
    ctx->read = std::bind(&Kcp::onReadEvent, kcp.get());
    ctx->scheduler = this;
    ev.data.ptr = ctx.get();
    ev.events = EPOLLET | EPOLLIN;
    int32_t rt = epoll_ctl(m_epollFd, EPOLL_CTL_ADD, sockFd, &ev);
    if (rt < 0) {
        ctx->resetContext(READ);
    }
    return rt == 0;
}

bool KcpManager::delKcp(Kcp::SP kcp)
{
    if (kcp == nullptr) {
        return false;
    }

    int32_t sockFd = kcp->m_updSocket;
    int32_t rt = epoll_ctl(m_epollFd, EPOLL_CTL_DEL, sockFd, nullptr);
    if (rt < 0) {
        LOGE("epoll_ctl error. [%d,%s]", errno, strerror(errno));
        return false;
    }
    Context::SP ctx;
    {
        AutoLock<Mutex> lock(m_ctxMutex);
        ctx = m_contextMap[sockFd];
        LOG_ASSERT2(ctx != nullptr);
        ctx->resetContext(READ);
        ctx->resetContext(WRITE);
    }
    return true;
}

void KcpManager::start()
{
    if (m_keepRun) {
        return;
    }
    m_keepRun = true;

    if (m_userCaller) {
        g_pKcpManager = this;
        threadloop();
    } else {
        m_thread = std::make_shared<Thread>(std::bind(&KcpManager::threadloop, this));
    }
}

void KcpManager::stop()
{
    if (m_keepRun == false) {
        return;
    }

    m_keepRun = false;
    KScheduler::stop();

    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
}

KcpManager *KcpManager::GetCurrentKcpManager()
{
    return g_pKcpManager;
}

void KcpManager::threadloop()
{
    g_pKcpManager = this;

    epoll_event *events = new epoll_event[EPOLL_EVENT_SIZE];
    std::shared_ptr<epoll_event> ptr(events, [](epoll_event *p) {
        if (p) {
            delete[] p;
        }
    });
    LOG_ASSERT2(events != nullptr);

    uint64_t timeoutms = 10;
    while (m_keepRun) {
        {
            eventfd_t count;
            eventfd_read(m_eventFd, &count);
        }

        timeoutms = getNearTimeout();
        int32_t nev = 0;
        do {
            nev = epoll_wait(m_epollFd, events, EPOLL_EVENT_SIZE, timeoutms);
            if (nev < 0 && errno == EINTR) {
            } else {
                break;
            }
        } while (true);

        if (nev < 0) {
            LOGE("epoll_wait error. [%d, %s]", errno, strerror(errno));
            break;
        }

        // 获取超时定时器并将其压到队列
        std::list<KTimer::CallBack> cbs;
        listExpiredTimer(cbs);
        schedule(cbs.begin(), cbs.end());

        // 处理事件
        for (int32_t i = 0; i < nev; ++i) {
            epoll_event &ev = events[i];
            if (ev.data.fd == m_eventFd) {
                eventfd_t value;
                eventfd_read(m_eventFd, &value);
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

void KcpManager::tickle()
{
    eventfd_write(m_eventFd, 1);
}

void KcpManager::onTimerInsertedAtFront()
{
    tickle();
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
        break;
    }
}

void KcpManager::Context::triggerEvent(Event event)
{
    switch (event) {
    case READ:
        scheduler->schedule(read);
        break;
    case WRITE:
        scheduler->schedule(write);
    default:
        break;
    }
}

} // namespace eular
