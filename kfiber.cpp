/*************************************************************************
    > File Name: kfiber.cpp
    > Author: hsz
    > Brief:
    > Created Time: Fri 08 Jul 2022 11:40:55 PM CST
 ************************************************************************/

#include "kfiber.h"

#include <atomic>
#include <exception>

#include <log/log.h>

#define LOG_TAG "KFiber"

static std::atomic<uint64_t> gFiberId(0);       // 协程ID
static std::atomic<uint64_t> gFiberCount(0);    // 当前协程总数

namespace eular {
static thread_local KFiber *gCurrentFiber = nullptr;         // 当前正在执行的协程
static thread_local KFiber::SP gThreadMainFiber = nullptr;   // 一个线程的主协程

uint64_t getStackSize()
{
    static uint64_t size = 1024 * 128;
    return size;
}

class MallocAllocator
{
public:
    static void *alloc(uint64_t size)
    {
        return malloc(size);
    }
    static void dealloc(void *ptr, uint64_t size)
    {
        LOG_ASSERT(ptr, "dealloc a null pointer");
        free(ptr);
    }
};

using Allocator = MallocAllocator;

KFiber::KFiber() :
    mFiberId(++gFiberId)
{
    ++gFiberCount;
    mState = EXEC;
    if (getcontext(&mCtx)) {
        LOG_ASSERT(false, "getcontext error, %d %s", errno, strerror(errno));
    }
    SetThis(this);
    LOGD("KFiber::KFiber() start id = %d, total = %d", mFiberId, gFiberCount.load());
}

KFiber::KFiber(std::function<void()> cb, uint64_t stackSize) :
    mState(READY),
    mFiberId(++gFiberId),
    mCb(cb)
{
    ++gFiberCount;

    mStackSize = stackSize ? stackSize : getStackSize();
    mStack = Allocator::alloc(mStackSize);
    LOG_ASSERT(mStack, "KFiber id = %lu, stack pointer is null", mFiberId);

    if (getcontext(&mCtx)) {
        LOG_ASSERT(false, "KFiber::KFiber(std::function<void()>, uint64_t) getcontext error. %d %s",
            errno, strerror(errno));
    }
    mCtx.uc_stack.ss_sp = mStack;
    mCtx.uc_stack.ss_size = mStackSize;
    mCtx.uc_link = nullptr;
    makecontext(&mCtx, &FiberEntry, 0);

    LOGD("KFiber::KFiber(std::function<void()>, uint64_t) id = %lu, total = %d", mFiberId, gFiberCount.load());
}

KFiber::~KFiber()
{
    --gFiberCount;
    LOGD("KFiber::~KFiber() id = %lu, total = %d", mFiberId, gFiberCount.load());
    if (mStack) {
        LOG_ASSERT(mState == TERM || mState == EXCEPT, "file %s, line %d", __FILE__, __LINE__);
        Allocator::dealloc(mStack, mStackSize);
    } else {    // main fiber
        LOG_ASSERT(!mCb, "");
        if (gCurrentFiber == this) {
            SetThis(nullptr);
        }
    }
}

// 调用位置在主协程中。
void KFiber::reset(std::function<void()> cb)
{
    LOG_ASSERT(mStack, "main fiber can't reset"); // 排除main fiber
    // 暂停态，执行态，ready态无法reset
    LOG_ASSERT(mState == TERM || mState == EXCEPT || mState == READY,
        "reset unauthorized operation");
    mCb = cb;
    if (getcontext(&mCtx)) {
        LOG_ASSERT(false, "File %s, Line %s. getcontex error.", __FILE__, __LINE__);
    }
    mCtx.uc_stack.ss_sp = mStack;
    mCtx.uc_stack.ss_size = mStackSize;
    mCtx.uc_link = nullptr;
    makecontext(&mCtx, &FiberEntry, 0);
    mState = READY;
}

void KFiber::swapIn()
{
    SetThis(this);
    LOG_ASSERT(mState != EXEC, "");
    mState = EXEC;
    if (swapcontext(&gThreadMainFiber->mCtx, &mCtx)) {
        LOG_ASSERT(false, "swapIn() id = %d, errno = %d, %s", mFiberId, errno, strerror(errno));
    }
}

void KFiber::swapOut()
{
    SetThis(gThreadMainFiber.get());
    if (swapcontext(&mCtx, &gThreadMainFiber->mCtx)) {
        LOG_ASSERT(false, "swapOut() id = %d, errno = %d, %s", mFiberId, errno, strerror(errno));
    }
}

void KFiber::SetThis(KFiber *f)
{
    gCurrentFiber = f;
}

KFiber::SP KFiber::GetThis()
{
    if (gCurrentFiber) {
        return gCurrentFiber->shared_from_this();
    }
    KFiber::SP fiber(new KFiber());
    LOG_ASSERT(fiber.get() == gCurrentFiber, "");
    gThreadMainFiber = fiber;
    LOG_ASSERT(gThreadMainFiber, "");
    return gCurrentFiber->shared_from_this();
}

void KFiber::call()
{
    SetThis(this);
    mState = EXEC;
    if (swapcontext(&gThreadMainFiber->mCtx, &mCtx)) {
        LOG_ASSERT(false, "call() id = %d, errno = %d, %s", mFiberId, errno, strerror(errno));
    }
}

void KFiber::back()
{
    SetThis(gThreadMainFiber.get());
    if (swapcontext(&mCtx, &gThreadMainFiber->mCtx)) {
        LOG_ASSERT(false, "back() id = %d, errno = %d, %s", mFiberId, errno, strerror(errno));
    }
}

void KFiber::resume()
{
    swapIn();
}

void KFiber::Yeild2Hold()
{
    KFiber::SP ptr = GetThis();
    LOG_ASSERT(ptr != nullptr, "");
    LOG_ASSERT(ptr->mState == EXEC, "");
    ptr->mState = HOLD;
    ptr->swapOut();
}

void KFiber::Yeild2Ready()
{
    KFiber::SP ptr = GetThis();
    LOG_ASSERT(ptr != nullptr, "");
    LOG_ASSERT(ptr->mState == EXEC, "");
    ptr->mState = READY;
    ptr->swapOut();
}

KFiber::FiberState KFiber::getState()
{
    return mState;
}

uint64_t KFiber::GetFiberID()
{
    if (gCurrentFiber) {
        return gCurrentFiber->mFiberId;
    }
    return 0;
}

void KFiber::FiberEntry()
{
    KFiber::SP curr = GetThis();
    LOG_ASSERT(curr != nullptr, "");
    try {
        curr->mCb();
        curr->mCb = nullptr;
        curr->mState = TERM;
    } catch (const std::exception& e) {
        curr->mState = EXCEPT;
        LOGE("KFiber except: %s; id = %lu", e.what(), curr->mFiberId);
    } catch (...) {
        curr->mState = EXCEPT;
        LOGE("KFiber except. id = %lu", curr->mFiberId);
    }

    KFiber *ptr = curr.get();
    curr.reset();   // 必须调用reset，否则shared_ptr会一直增一，因为未超出作用域
    LOG_ASSERT(ptr != nullptr, "will never be null to reach here");
    ptr->swapOut();

    LOG_ASSERT(false, "never reach here");
}

} // namespace eular
