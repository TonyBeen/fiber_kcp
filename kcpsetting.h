/*************************************************************************
    > File Name: kcpsetting.h
    > Author: hsz
    > Brief:
    > Created Time: Sun 09 Jun 2024 08:38:11 PM CST
 ************************************************************************/

#ifndef __KCP_SETTING_H__
#define __KCP_SETTING_H__

#include <stdint.h>
#include <string.h>

#include <arpa/inet.h>

struct KcpSetting
{
    int32_t     fd;             // udp套接字描述符
    uint32_t    conv;           // 会话号
    uint16_t    send_wnd_size;  // 发送窗口大小(默认512)
    uint16_t    recv_wnd_size;  // 接收窗口大小(默认512)
    uint8_t     nodelay;        // 0:关闭无延迟ACK(默认), 1:开启
    int32_t     interval;       // 更新时间间隔(默认40)
    uint8_t     fast_resend;    // 0:关闭快速重传(default), >0:开启快速重传(默认2, 两次ACK跳过就重传)
    uint8_t     nocwnd;         // 拥塞控制
    sockaddr_in remote_addr;    // 远端地址
    uint32_t    heart_beat;     // 心跳检测, 0表示不开启, >0, 表示开启心跳检测

    KcpSetting() :
        fd(-1), conv(0),
        send_wnd_size(512), recv_wnd_size(512),
        nodelay(1), interval(40), fast_resend(2),
        nocwnd(1), heart_beat(0)
    {
        memset(&remote_addr, 0, sizeof(remote_addr));
    }
};

static inline void kcp_normal_mode(KcpSetting *setting)
{
    setting->nodelay = 0;
    setting->interval = 40;
    setting->fast_resend = 0;
    setting->nocwnd = 0;
}

static inline void kcp_fast_mode(KcpSetting *setting)
{
    setting->nodelay = 0;
    setting->interval = 30;
    setting->fast_resend = 2;
    setting->nocwnd = 1;
}

static inline void kcp_fast2_mode(KcpSetting *setting)
{
    setting->nodelay = 1;
    setting->interval = 20;
    setting->fast_resend = 2;
    setting->nocwnd = 1;
}

static inline void kcp_fast3_mode(KcpSetting *setting)
{
    setting->nodelay = 1;
    setting->interval = 10;
    setting->fast_resend = 2;
    setting->nocwnd = 1;
}

#endif // __KCP_SETTING_H__
