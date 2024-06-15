/*************************************************************************
    > File Name: kcpprotocol.h
    > Author: hsz
    > Brief:
    > Created Time: Tue 11 Jun 2024 10:00:38 AM CST
 ************************************************************************/

#ifndef __KCP_PROTOCOL_H__
#define __KCP_PROTOCOL_H__

#include <stdint.h>

#define KCP_MAX_CONV    256
#ifndef MTU_SIZE
#define MTU_SIZE        1400
#endif

#define KCP_MASK        0xFFFFFF00u
#define KCP_FLAG        0x4B435000u     // 'K' 'C' 'P' 0x00

// NOTE KCP传输时采用小端字节序, 故按PCK存储
static const char KCP_ARRAY[] =  {'P', 'C', 'K'};

namespace protocol {

enum SYNCommand : int32_t {
    SYN = 0,    // 请求建立连接标志
    ACK = 1,    // 确认建立连接/断开连接/重置标志
    FIN = 2,    // 请求断开连接标志
    RST = 3,    // 请求重置, 即快速断开操作, 意味着不会等到缓存中的数据都发出去
};

enum KCPMode : int32_t {
    Normal  = 0,
    Fast    = 1,
    Fast2   = 2,
    Fast3   = 3,
};

// 保持和KCP头部相同字节数
struct KcpProtocol {
    uint32_t    kcp_conv;       // 交流号
    uint16_t    kcp_mode;       // kcp模式
    uint16_t    syn_command;    // 命令字段
    uint32_t    sn;             // 随机序列号
    uint32_t    send_win_size;  // 发送窗口大小
    uint32_t    recv_win_size;  // 接收窗口大小
    uint32_t    reserve;        // 用于客户端发送FIN/RST, 携带的交流号
};
static const uint32_t KCP_PROTOCOL_SIZE = sizeof(KcpProtocol);

static inline bool IsLittleEngine();
static inline uint32_t GetKcpFlag();
static inline void InitKcpProtocol(KcpProtocol *pKcpProtocol);
static inline void SerializeKcpProtocol(KcpProtocol *pKcpProtocol, void *pBuffer);
static inline void DeserializeKcpProtocol(const void *pBuffer, KcpProtocol *pKcpProtocol);
static inline const uint8_t *DecodeUINT16(const uint8_t *p, uint16_t *w);
static inline const uint8_t *DecodeUINT32(const uint8_t *p, uint32_t *l);

} // namespace protocol

#endif // __KCP_PROTOCOL_H__
