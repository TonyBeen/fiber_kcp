/*************************************************************************
    > File Name: thread.h
    > Author: hsz
    > Brief:
    > Created Time: Sun 27 Feb 2022 04:47:02 PM CST
 ************************************************************************/

#ifndef __THREAD_H__
#define __THREAD_H__

#include <utils/mutex.h>
#include <pthread.h>
#include <stdint.h>
#include <string>
#include <functional>
#include <memory>

namespace eular {
class Thread
{
public:
    typedef std::weak_ptr<Thread>   WP;
    typedef std::shared_ptr<Thread> SP;
    typedef std::unique_ptr<Thread> Ptr;
    typedef std::function<void()>   ThreadCB;

    Thread(ThreadCB cb, const eular::String8 &threadName = "", uint32_t stackSize = 0);
    ~Thread();

    static Thread*      CurrentThread();
    static void         SetThreadName(eular::String8 name);
    static String8      GetThreadName();
    eular::String8      getName() const { return m_name; }
    pid_t               getTid() const { return m_tid; };

    void detach();
    bool joinable() { return m_joinable; }
    void join();

protected:
    static void *Entrance(void *arg);

private:
    pid_t               m_tid;          // 内核tid
    pthread_t           m_pthreadId;    // pthread线程ID
    eular::String8      m_name;
    ThreadCB            m_cb;           // 线程执行函数
    uint8_t             m_joinable;     // 1为由用户回收线程，0为自动回收
    eular::Sem          m_semaphore;    // 等待mKernalTid赋值
};

} // namespace eular
#endif // __THREAD_H__
