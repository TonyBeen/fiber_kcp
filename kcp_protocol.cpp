/*************************************************************************
    > File Name: kcp_protocol.cpp
    > Author: hsz
    > Brief:
    > Created Time: Tue 11 Jun 2024 10:37:04 AM CST
 ************************************************************************/

#include "kcp_protocol.h"

#include <mutex>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <utils/sysdef.h>

#include "endian.hpp"

#ifndef BYTE_ORDER
static std::once_flag g_endianOnceFlag;

static inline void __IsLittleEngine()
{
    const uint16_t NUMBER = 0x1122;
    const uint8_t  FIRST_BYTE = 0x22;
    union {
        uint8_t     oneByte;
        uint16_t    twoByte;
    } container;
    container.twoByte = NUMBER;

    g_isLittleEngine = (container.oneByte == FIRST_BYTE);
}

#endif
static bool g_isLittleEngine = false;

namespace protocol {

static inline bool IsLittleEngine()
{
#ifndef BYTE_ORDER
    std::call_once(g_endianOnceFlag, __IsLittleEngine);
#else
#if BYTE_ORDER == LITTLE_EDNIAN
    g_isLittleEngine = true;
#endif
#endif
    return g_isLittleEngine;
}

static inline uint32_t GetKcpFlag()
{
#ifndef BYTE_ORDER
    if (IsLittleEngine())
    {
        return KCP_FLAG;
    }
    else
    {
        return ntohl(KCP_FLAG);
    }
#else
    return htole32(KCP_FLAG);
#endif
}

static inline void InitKcpProtocol(KcpProtocol *pKcpProtocol)
{
    pKcpProtocol->kcp_conv = KCP_FLAG;
    pKcpProtocol->kcp_mode = KCPMode::Normal;
    pKcpProtocol->syn_command = SYNCommand::ACK;
    pKcpProtocol->sn = 0;
    pKcpProtocol->send_win_size = 0;
    pKcpProtocol->recv_win_size = 0;
    pKcpProtocol->reserve = 0;
}

static inline void SerializeKcpProtocol(KcpProtocol *pKcpProtocol, void *pBuffer)
{
#ifndef BYTE_ORDER
    if (!IsLittleEngine()) {
        pKcpProtocol->kcp_conv = htole32(pKcpProtocol->kcp_conv);
        pKcpProtocol->kcp_mode = htole16(pKcpProtocol->kcp_mode);
        pKcpProtocol->syn_command = htole16(pKcpProtocol->syn_command);
        pKcpProtocol->sn = htole32(pKcpProtocol->sn);
        pKcpProtocol->send_win_size = htole32(pKcpProtocol->send_win_size);
        pKcpProtocol->recv_win_size = htole32(pKcpProtocol->recv_win_size);
        pKcpProtocol->reserve = htole32(pKcpProtocol->reserve);
    }
#else
    pKcpProtocol->kcp_conv = htole32(pKcpProtocol->kcp_conv);
    pKcpProtocol->kcp_mode = htole16(pKcpProtocol->kcp_mode);
    pKcpProtocol->syn_command = htole16(pKcpProtocol->syn_command);
    pKcpProtocol->sn = htole32(pKcpProtocol->sn);
    pKcpProtocol->send_win_size = htole32(pKcpProtocol->send_win_size);
    pKcpProtocol->recv_win_size = htole32(pKcpProtocol->recv_win_size);
    pKcpProtocol->reserve = htole32(pKcpProtocol->reserve);
#endif

    memcpy(pBuffer, pKcpProtocol, KCP_PROTOCOL_SIZE);
}

static inline void DeserializeKcpProtocol(const void *pBuffer, KcpProtocol *pKcpProtocol)
{
    const KcpProtocol *pTemp = (const KcpProtocol *)pBuffer;
#ifndef BYTE_ORDER
    if (!IsLittleEngine())
    {
        pKcpProtocol->kcp_conv = le32toh(pTemp->kcp_conv);
        pKcpProtocol->kcp_mode = le16toh(pTemp->kcp_mode);
        pKcpProtocol->syn_command = le16toh(pTemp->syn_command);
        pKcpProtocol->sn = le32toh(pTemp->sn);
        pKcpProtocol->send_win_size = le32toh(pTemp->send_win_size);
        pKcpProtocol->recv_win_size = le32toh(pTemp->recv_win_size);
        pKcpProtocol->reserve = le32toh(pTemp->reserve);
    }
#else
    pKcpProtocol->kcp_conv = le32toh(pTemp->kcp_conv);
    pKcpProtocol->kcp_mode = le16toh(pTemp->kcp_mode);
    pKcpProtocol->syn_command = le16toh(pTemp->syn_command);
    pKcpProtocol->sn = le32toh(pTemp->sn);
    pKcpProtocol->send_win_size = le32toh(pTemp->send_win_size);
    pKcpProtocol->recv_win_size = le32toh(pTemp->recv_win_size);
    pKcpProtocol->reserve = le32toh(pTemp->reserve);
#endif
}

static inline const uint8_t *DecodeUINT16(const uint8_t *p, uint16_t *w)
{
    static_assert(sizeof(uint16_t) == 2, "");

#if BYTE_ORDER == BIG_ENDIAN
    *w = *(uint8_t *)(p + 1);
    *w = *(uint8_t *)(p + 0) + (*w << 8);
#else
    memcpy(w, p, sizeof(uint16_t));
#endif

    p += sizeof(uint16_t);
    return p;
}

static inline const uint8_t *DecodeUINT32(const uint8_t *p, uint32_t *l)
{
    static_assert(sizeof(uint32_t) == 4, "");

#if BYTE_ORDER == BIG_ENDIAN
    *l = *(uint8_t *)(p + 3);
    *l = *(uint8_t *)(p + 2) + (*l << 8);
    *l = *(uint8_t *)(p + 1) + (*l << 8);
    *l = *(uint8_t *)(p + 0) + (*l << 8);
#else
    memcpy(l, p, sizeof(uint32_t));
#endif

    p += sizeof(uint32_t);
    return p;
}

} // namespace protocol

