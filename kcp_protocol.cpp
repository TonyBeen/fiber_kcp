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

#include <utils/endian.hpp>

#include "kcp_setting.h"

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

bool IsLittleEngine()
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

uint32_t GetKcpFlag()
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

void InitKcpProtocol(KcpProtocol *pKcpProtocol)
{
    pKcpProtocol->kcp_flag = KCP_FLAG;
    pKcpProtocol->kcp_mode = KCPMode::Normal;
    pKcpProtocol->syn_command = SYNCommand::ACK;
    pKcpProtocol->sn = 0;
    pKcpProtocol->send_win_size = 0;
    pKcpProtocol->recv_win_size = 0;
    pKcpProtocol->kcp_conv = 0;
}

void SerializeKcpProtocol(KcpProtocol *pKcpProtocol, void *pBuffer)
{
#ifndef BYTE_ORDER
    if (!IsLittleEngine()) {
        pKcpProtocol->kcp_flag = htole32(pKcpProtocol->kcp_flag);
        pKcpProtocol->kcp_mode = htole16(pKcpProtocol->kcp_mode);
        pKcpProtocol->syn_command = htole16(pKcpProtocol->syn_command);
        pKcpProtocol->sn = htole32(pKcpProtocol->sn);
        pKcpProtocol->send_win_size = htole32(pKcpProtocol->send_win_size);
        pKcpProtocol->recv_win_size = htole32(pKcpProtocol->recv_win_size);
        pKcpProtocol->kcp_conv = htole32(pKcpProtocol->kcp_conv);
    }
#else
    pKcpProtocol->kcp_flag = htole32(pKcpProtocol->kcp_flag);
    pKcpProtocol->kcp_mode = htole16(pKcpProtocol->kcp_mode);
    pKcpProtocol->syn_command = htole16(pKcpProtocol->syn_command);
    pKcpProtocol->sn = htole32(pKcpProtocol->sn);
    pKcpProtocol->send_win_size = htole32(pKcpProtocol->send_win_size);
    pKcpProtocol->recv_win_size = htole32(pKcpProtocol->recv_win_size);
    pKcpProtocol->kcp_conv = htole32(pKcpProtocol->kcp_conv);
#endif

    memcpy(pBuffer, pKcpProtocol, KCP_PROTOCOL_SIZE);
}

void DeserializeKcpProtocol(const void *pBuffer, KcpProtocol *pKcpProtocol)
{
    const KcpProtocol *pTemp = (const KcpProtocol *)pBuffer;
#ifndef BYTE_ORDER
    if (!IsLittleEngine())
    {
        pKcpProtocol->kcp_flag = le32toh(pTemp->kcp_flag);
        pKcpProtocol->kcp_mode = le16toh(pTemp->kcp_mode);
        pKcpProtocol->syn_command = le16toh(pTemp->syn_command);
        pKcpProtocol->sn = le32toh(pTemp->sn);
        pKcpProtocol->send_win_size = le32toh(pTemp->send_win_size);
        pKcpProtocol->recv_win_size = le32toh(pTemp->recv_win_size);
        pKcpProtocol->kcp_conv = le32toh(pTemp->kcp_conv);
    }
#else
    pKcpProtocol->kcp_flag = le32toh(pTemp->kcp_flag);
    pKcpProtocol->kcp_mode = le16toh(pTemp->kcp_mode);
    pKcpProtocol->syn_command = le16toh(pTemp->syn_command);
    pKcpProtocol->sn = le32toh(pTemp->sn);
    pKcpProtocol->send_win_size = le32toh(pTemp->send_win_size);
    pKcpProtocol->recv_win_size = le32toh(pTemp->recv_win_size);
    pKcpProtocol->kcp_conv = le32toh(pTemp->kcp_conv);
#endif
}

const uint8_t *DecodeUINT16(const uint8_t *p, uint16_t *w)
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

const uint8_t *DecodeUINT32(const uint8_t *p, uint32_t *l)
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
