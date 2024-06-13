/*************************************************************************
    > File Name: heap_timer.h
    > Author: hsz
    > Brief:
    > Created Time: Thu 06 Jun 2024 10:52:36 AM CST
 ************************************************************************/

#ifndef __KCP_HEAP_TIMER_H__
#define __KCP_HEAP_TIMER_H__

#include <stdint.h>
#include <atomic>

#include "heap.h"

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

#define HEAP_NODE_TO_TIMER(ptr) \
    container_of(ptr, heap_timer_t, node)

typedef struct heap_timer {
    heap_node_t         node;
    uint32_t            tid;            // 当前定时器所在线程ID
    uint32_t            recycle_time;   // 定时器循环时间(0表示单次定时器)
    uint64_t            next_timeout;   // 下次超时时间
    uint64_t            unique_id;      // 唯一ID
    void *              user_data;      // 用户数据

#ifdef __cplusplus
    heap_timer() {
        node.left = nullptr;
        node.right = nullptr;
        node.parent = nullptr;
    }
#endif

} heap_timer_t;

#endif // __KCP_HEAP_TIMER_H__
