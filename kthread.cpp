/*************************************************************************
    > File Name: thread.cpp
    > Author: hsz
    > Brief:
    > Created Time: Sun 27 Feb 2022 04:47:10 PM CST
 ************************************************************************/

#include "kthread.h"

#include <sys/resource.h>

#include <utils/utils.h>
#include <utils/exception.h>
#include <log/log.h>

#define LOG_TAG "Thread"

namespace eular {

static thread_local Thread *gThread = nullptr;      // 当前线程
static thread_local eular::String8 gThreadName;     // 当前线程名字

pthread_once_t g_onceControl = PTHREAD_ONCE_INIT;
static uint64_t g_maxThreadStackSize = PTHREAD_STACK_MIN;

void GetMaxThreadStackSize()
{
    struct rlimit rlim;
    // 获取线程栈大小的最大限制
    if (getrlimit(RLIMIT_STACK, &rlim) == 0) {
        if (rlim.rlim_cur == RLIM_INFINITY) {
            g_maxThreadStackSize = UINT64_MAX;
        } else {
            g_maxThreadStackSize = rlim.rlim_cur;
        }
    }
}

Thread::Thread(ThreadCB cb, const eular::String8 &threadName, uint32_t stackSize) :
    m_pthreadId(0),
    m_name(threadName.length() ? threadName : "Unknow"),
    m_cb(cb),
    m_joinable(true),
    m_semaphore(0)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stackSize) {
        pthread_once(&g_onceControl, GetMaxThreadStackSize);
        if (stackSize < PTHREAD_STACK_MIN) {
            stackSize = PTHREAD_STACK_MIN;
        } else if (stackSize > g_maxThreadStackSize) {
            stackSize = g_maxThreadStackSize;
        }

        pthread_attr_setstacksize(&attr, stackSize);
    }

    int ret = pthread_create(&m_pthreadId, &attr, &Thread::Entrance, this);
    pthread_attr_destroy(&attr);
    if (ret) {
        LOGE("pthread_create error. [%d,%s]", errno, strerror(errno));
        throw eular::Exception("pthread_create error");
    }
    m_semaphore.wait();
}

Thread::~Thread()
{
    join();
}

void Thread::SetThreadName(eular::String8 name)
{
    if (name.empty()) {
        return;
    }

    if (gThread) {
        gThread->m_name = name;
    }
    gThreadName = name;
}

eular::String8 Thread::GetThreadName()
{
    return gThreadName;
}

Thread *Thread::CurrentThread()
{
    return gThread;
}

void Thread::detach()
{
    if (m_pthreadId) {
        pthread_detach(m_pthreadId);
        m_joinable = false;
    }
}

void Thread::join()
{
    if (m_joinable && m_pthreadId) {
        int ret = pthread_join(m_pthreadId, nullptr);
        if (ret) {
            LOGE("pthread_join error. [%d,%s]", errno, strerror(errno));
            throw eular::Exception("pthread_join error");
        }
        m_pthreadId = 0;
        m_joinable = false;
    }
}

void *Thread::Entrance(void *arg)
{
    LOG_ASSERT(arg, "Thread::Entrance: arg never be null");
    Thread *th = static_cast<Thread *>(arg);
    gThread = th;
    gThreadName = th->m_name;
    gThread->m_tid = gettid();
    gThread->m_semaphore.post();

    pthread_setname_np(pthread_self(), th->m_name.substr(0, 15).c_str());

    std::function<void()> cb;
    cb.swap(th->m_cb);

    cb();
    gThread = nullptr;
    return nullptr;
}

} // namespace eular
