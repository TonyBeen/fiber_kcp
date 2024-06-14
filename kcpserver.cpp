/*************************************************************************
    > File Name: kcpserver.cpp
    > Author: hsz
    > Brief:
    > Created Time: Fri 07 Jun 2024 06:00:37 PM CST
 ************************************************************************/

#define _GNU_SOURCE
#include "kcpserver.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <utils/utils.h>
#include <utils/exception.h>
#include <log/log.h>

#include "ikcp.h"
#include "kcpmanager.h"
#include "kcp_utils.h"

#define LOG_TAG "KcpServer"

#define MIN_TIMEOUT 100
#define MAX_TIMEOUT 5000

namespace eular {
KcpServer::KcpServer() :
    m_connectTimeout(3000),
    m_disconnectTimeout(3000)
{
    m_kcpConvBitmap.reserve(KCP_MAX_CONV / BITS_PEER_BYTE);
    m_kcpConvBitmap.set(0, true);
}

KcpServer::~KcpServer()
{
}

void KcpServer::installConnectEvent(ConnectEventCB connectEventCB) noexcept
{
    m_connectEventCB = connectEventCB;
}

void KcpServer::setConnectTimeout(uint32_t timeout) noexcept
{
    if (timeout < MIN_TIMEOUT) {
        timeout = MIN_TIMEOUT;
    } else if (timeout > MAX_TIMEOUT) {
        timeout = MAX_TIMEOUT;
    }

    m_connectTimeout = timeout;
}

void KcpServer::setDisconnectTimeout(uint32_t timeout) noexcept
{
    if (timeout < MIN_TIMEOUT) {
        timeout = MIN_TIMEOUT;
    } else if (timeout > MAX_TIMEOUT) {
        timeout = MAX_TIMEOUT;
    }

    m_disconnectTimeout = timeout;
}

void KcpServer::onReadEvent()
{
    LOGW("%s <begin>", __PRETTY_FUNCTION__);
    static_assert(KCP_HEADER_SIZE == protocol::KCP_PROTOCOL_SIZE);

    uint8_t headerBuffer[KCP_HEADER_SIZE] = {0};

    sockaddr_in peerAddr;
    socklen_t len = sizeof(sockaddr_in);
    bool hasError = false;

    uint32_t kcpFlag = 0;

    // 预留8KB字节
    static thread_local ByteBuffer kcpBuffer(8 * 1024);

    uint32_t canReadSize = 0;
    ioctl(m_updSocket, FIONREAD, &canReadSize);
    kcpBuffer.reserve(canReadSize);
    kcpBuffer.clear();

    do {
        int32_t realReadSize = TEMP_FAILURE_RETRY(::recvfrom(m_updSocket, kcpBuffer.data(), kcpBuffer.capacity(), 0,
                                                            (sockaddr *)&peerAddr, &len));
        if (realReadSize < 0) {
            if (errno != EAGAIN) {
                LOGE("recvfrom error. [%d,%s]", error, strerror(errno));
            }
            break;
        }

        // 杂数据
        if (realReadSize < KCP_HEADER_SIZE) {
            continue;
        }

        kcpBuffer.resize(realReadSize);
        uint8_t *pHeaderBuf = kcpBuffer.data();
        uint8_t *pFound = (uint8_t *)memmem(pHeaderBuf, kcpBuffer.size(), KCP_ARRAY, sizeof(KCP_ARRAY));
        if (pFound == nullptr) {
            char ipv4Host[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &peerAddr.sin_addr, ipv4Host, INET_ADDRSTRLEN);
            LOGW("receive %zu bytes from [%s:%d] will ignore.", kcpBuffer.size(), ipv4Host, ntohs(peerAddr.sin_port));
            // 未找到KCP标志, 忽略此地址发送的数据
            kcpBuffer.clear();
            continue;
        }

        // 存在无关数据, 需要将无关数据剔除
        if (pFound != pHeaderBuf) {
            pHeaderBuf = pFound;
        }

        // 解析协议
        protocol::KcpProtocol kcpProtoInput;
        protocol::DeserializeKcpProtocol(pHeaderBuf, &kcpProtoInput);

        LOGI("kcp conv = %#x", kcpProtoInput.kcp_conv);

        if (kcpProtoInput.kcp_conv == KCP_FLAG) {
            // 处理连接/断连请求
            switch (kcpProtoInput.syn_command) {
            case protocol::SYNCommand::SYN:
                onSYNReceived(&kcpProtoInput, peerAddr);
                break;
            case protocol::SYNCommand::ACK:
                onACKReceived(&kcpProtoInput, peerAddr);
                break;
            case protocol::SYNCommand::FIN:
                onFINReceived(&kcpProtoInput, peerAddr);
                break;
            case protocol::SYNCommand::RST:
                break;
            default:
                break;
            }
        } else if (kcpProtoInput.kcp_conv & KCP_FLAG == KCP_FLAG) {
            // 处理KCP数据
            onKcpDataReceived(pHeaderBuf, kcpProtoInput.kcp_conv, peerAddr);
        }
        kcpBuffer.clear();
    } while (true);
    LOGW("%s <end>", __PRETTY_FUNCTION__);
}

// TODO UDP面向无连接, 使用recvfrom读取需要先将缓存读取完毕, 并按照addr进行划分.
// 第一次recvfrom可能从A地址读, 下一次recvfrom可能从B地址读
void KcpServer::onKcpDataReceived(const uint8_t *pHeaderBuf, uint32_t conv, sockaddr_in peerAddr)
{
    int32_t realReadSize = 0;
    socklen_t len = sizeof(sockaddr_in);
    ByteBuffer kcpDataBuffer(MTU_SIZE);
    kcpDataBuffer.append(pHeaderBuf, KCP_HEADER_SIZE);

    pHeaderBuf += 20; // 偏移至KCP头部的len字段
    uint32_t dataSize = 0;
    protocol::DecodeUINT32(pHeaderBuf, &dataSize);
    // NOTE KCP数据会被分片, 故数据不会超过 MTU_SIZE - KCP_HEADER_SIZE. 但是不排除杂数据正好满足KCP_FLAG且数据长度过大, 导致申请内存失败
    LOG_ASSERT2(dataSize < 512 * 1024);
    kcpDataBuffer.reserve(KCP_HEADER_SIZE + dataSize);
    if (dataSize > 0) {
        realReadSize = TEMP_FAILURE_RETRY(::recvfrom(m_updSocket, kcpDataBuffer.data() + KCP_HEADER_SIZE, kcpDataBuffer.capacity(), 0,
            (sockaddr *)&peerAddr, &len));
        if (realReadSize < 0) {
            if (errno != EAGAIN) {
                LOGE("recvfrom error: %d, %s", errno, strerror(errno));
            }
            return;
        }
    }
    kcpDataBuffer.resize(KCP_HEADER_SIZE + realReadSize);

    // 如果从半连接队列找到此会话, 关闭定时器并添加到Map
    for (const auto &synIt : m_synConnectQueue) {
        if (synIt.conv == conv) {
            KcpManager::GetCurrentKcpManager()->delTimer(synIt.timerId);
            KcpContext::SP spContext = std::make_shared<KcpContext>();
            KcpSetting setting;
            switch (synIt.syn_protocol.kcp_mode) {
            case protocol::KCPMode::Fast:
                kcp_fast_mode(&setting);
                break;
            case protocol::KCPMode::Fast2:
                kcp_fast2_mode(&setting);
                break;
            case protocol::KCPMode::Fast3:
                kcp_fast3_mode(&setting);
                break;
            default:
                kcp_normal_mode(&setting);
                break;
            }

            setting.fd = m_updSocket;
            setting.conv = conv;
            setting.send_wnd_size = synIt.syn_protocol.send_win_size;
            setting.recv_wnd_size = synIt.syn_protocol.recv_win_size;
            setting.remote_addr = peerAddr;
            spContext->setSetting(setting);
            spContext->m_localHost = m_localHost;
            spContext->m_localPort = m_localPort;
            spContext->m_closeEvent = std::bind(&KcpServer::onKcpContextClosed, this, std::placeholders::_1);

            if (m_connectEventCB && m_connectEventCB(spContext)) {
                auto spTimer = KcpManager::GetCurrentKcpManager()->addTimer(setting.interval,
                    std::bind(&KcpContext::onUpdateTimeout, spContext.get()), setting.interval);
                spContext->m_timerId = spTimer->getUniqueId();
                m_kcpContextMap[conv] = spContext;
            }
            break;
        }
    }

    // 处理KCP数据
    auto ctxIt = m_kcpContextMap.find(conv);
    if (ctxIt != m_kcpContextMap.end()) {
        ctxIt->second->onRecv(kcpDataBuffer);
    }
}

// 连接请求
void KcpServer::onSYNReceived(protocol::KcpProtocol *pKcpProtocolReq, sockaddr_in peerAddr)
{
    LOGD("%s SYN Received from %s", __PRETTY_FUNCTION__, utils::Address2String((sockaddr *)&peerAddr));
    // 找到一个未使用的会话号
    uint32_t communicationNo = 0;
    for (uint32_t i = 0; i < KCP_MAX_CONV; ++i) {
        if (!m_kcpConvBitmap.at(i)) {
            communicationNo = i;
            m_kcpConvBitmap.set(i, true);
        }
    }

    communicationNo |= KCP_FLAG;

    protocol::KcpProtocol kcpProtoOutput;
    protocol::InitKcpProtocol(&kcpProtoOutput);
    uint8_t kcpProtoBuffer[protocol::KCP_PROTOCOL_SIZE] = {0};

    if (communicationNo == 0) {
        kcpProtoOutput.kcp_conv = KCP_FLAG;
        kcpProtoOutput.kcp_mode = pKcpProtocolReq->kcp_mode;
        kcpProtoOutput.syn_command = protocol::SYNCommand::FIN;
        kcpProtoOutput.sn = pKcpProtocolReq->sn;
        kcpProtoOutput.send_win_size = pKcpProtocolReq->send_win_size;
        kcpProtoOutput.recv_win_size = pKcpProtocolReq->recv_win_size;
        protocol::SerializeKcpProtocol(&kcpProtoOutput, kcpProtoBuffer);

        LOGW("Insufficient communication number, send FIN flag");

        // 会话号不足, 暂不接受连接请求
        ::sendto(m_updSocket, kcpProtoBuffer, protocol::KCP_PROTOCOL_SIZE, 0, (const sockaddr *)&peerAddr, sizeof(sockaddr_in));
        return;
    }

    LOGI("found conv = %#x", communicationNo);

    kcpProtoOutput.kcp_conv = communicationNo;
    kcpProtoOutput.kcp_mode = pKcpProtocolReq->kcp_mode;
    kcpProtoOutput.syn_command = protocol::SYNCommand::ACK;
    kcpProtoOutput.sn = pKcpProtocolReq->sn;
    kcpProtoOutput.send_win_size = pKcpProtocolReq->send_win_size;
    kcpProtoOutput.recv_win_size = pKcpProtocolReq->recv_win_size;

    KcpSYNInfo synInfo = {
        .timeout = m_connectTimeout,
        .conv = communicationNo,
        .peer_addr = peerAddr,
        .timerId = 0,
        .syn_protocol = kcpProtoOutput,
    };

    protocol::SerializeKcpProtocol(&kcpProtoOutput, kcpProtoBuffer);

    // 发送确认连接请求
    ::sendto(m_updSocket, kcpProtoBuffer, protocol::KCP_PROTOCOL_SIZE, 0, (const sockaddr *)&peerAddr, sizeof(sockaddr_in));

    auto spTimer = KcpManager::GetCurrentKcpManager()->addTimer(m_connectTimeout,
        std::bind(&KcpServer::onConnectTimeout, this, communicationNo));
    synInfo.timerId = spTimer->getUniqueId();

    m_synConnectQueue.push_back(synInfo);
}

// 确认断开连接请求
void KcpServer::onACKReceived(protocol::KcpProtocol *pKcpProtocolReq, sockaddr_in peerAddr)
{
    LOGD("%s ACK Received from %s", __PRETTY_FUNCTION__, utils::Address2String((sockaddr *)&peerAddr));
    KcpFINInfo finInfo = {
        .conv = 0
    };

    // NOTE FIN/ACK传递的都是
    for (auto it = m_finDisconnectQueue.begin(); it != m_finDisconnectQueue.end(); ++it) {
        if (it->sn == pKcpProtocolReq->sn &&
            it->peer_addr.sin_addr.s_addr == peerAddr.sin_addr.s_addr && 
            it->peer_addr.sin_port == peerAddr.sin_port) {
            finInfo = *it;
            m_finDisconnectQueue.erase(it);
        }
    }

    auto it = m_kcpContextMap.find(finInfo.conv);
    if (it != m_kcpContextMap.end()) {
        KcpManager::GetCurrentKcpManager()->delTimer(finInfo.timerId);
        KcpManager::GetCurrentKcpManager()->delTimer(it->second->m_timerId);

        m_kcpContextMap.erase(it);
    }
}

// 请求断开连接
void KcpServer::onFINReceived(protocol::KcpProtocol *pKcpProtocolReq, sockaddr_in peerAddr)
{
    LOGD("%s FIN Received from %s", __PRETTY_FUNCTION__, utils::Address2String((sockaddr *)&peerAddr));
    uint32_t conv = pKcpProtocolReq->reserve;
    protocol::KcpProtocol kcpProtoOutput;
    protocol::InitKcpProtocol(&kcpProtoOutput);
    uint8_t kcpProtoBuffer[protocol::KCP_PROTOCOL_SIZE] = {0};

    kcpProtoOutput.kcp_conv = conv;
    kcpProtoOutput.kcp_mode = pKcpProtocolReq->kcp_mode;
    kcpProtoOutput.syn_command = protocol::SYNCommand::ACK;
    kcpProtoOutput.sn = pKcpProtocolReq->sn;
    kcpProtoOutput.send_win_size = pKcpProtocolReq->send_win_size;
    kcpProtoOutput.recv_win_size = pKcpProtocolReq->recv_win_size;
    protocol::SerializeKcpProtocol(&kcpProtoOutput, kcpProtoBuffer);

    auto it = m_kcpContextMap.find(conv);
    if (it != m_kcpContextMap.end()) {
        KcpManager::GetCurrentKcpManager()->delTimer(it->second->m_timerId);

        // 主动调用更新数据
        it->second->onUpdateTimeout();
        m_kcpContextMap.erase(it);
    }

    ::sendto(m_updSocket, kcpProtoBuffer, KCP_HEADER_SIZE, 0, (sockaddr *)&peerAddr, sizeof(peerAddr));
}

// 重置请求
void KcpServer::onRSTReceived(protocol::KcpProtocol *pKcpProtocolReq, sockaddr_in peerAddr)
{
    LOGD("%s RST Received from %s", __PRETTY_FUNCTION__, utils::Address2String((sockaddr *)&peerAddr));

}

void KcpServer::onConnectTimeout(uint32_t conv)
{
    // 超时将其从半连接队列移除
    for (auto it = m_synConnectQueue.begin(); it != m_synConnectQueue.end(); ++it) {
        if (it->conv == conv) {
            // 将会话号加入备选
            uint32_t index = conv & ~KCP_MASK;
            m_kcpConvBitmap.set(index, false);

            m_synConnectQueue.erase(it);
        }
    }
}

void KcpServer::onDisconnectTimeout(uint32_t conv)
{
    auto it = m_kcpContextMap.find(conv);
    if (it != m_kcpContextMap.end()) {
        KcpManager::GetCurrentKcpManager()->delTimer(it->second->m_timerId);

        m_kcpContextMap.erase(it);
    }
}

void KcpServer::onKcpContextClosed(KcpContext::SP spContext)
{
    // 主动关闭Context需要发送FIN
    protocol::KcpProtocol kcpProtoOutput;
    protocol::InitKcpProtocol(&kcpProtoOutput);

    // 以时间戳作为序列号
    uint32_t timeS = static_cast<uint32_t>(time(NULL));

    kcpProtoOutput.kcp_conv = KCP_FLAG;
    kcpProtoOutput.syn_command = protocol::SYNCommand::FIN;
    kcpProtoOutput.sn = timeS;
    uint8_t buffer[protocol::KCP_PROTOCOL_SIZE] = {0};
    protocol::SerializeKcpProtocol(&kcpProtoOutput, buffer);

    ::sendto(m_updSocket, buffer, protocol::KCP_PROTOCOL_SIZE, 0,
        (sockaddr *)&spContext->m_setting.remote_addr, sizeof(sockaddr_in));

    auto spTimer = KcpManager::GetCurrentKcpManager()->addTimer(m_disconnectTimeout,
        std::bind(&KcpServer::onDisconnectTimeout, this, spContext->m_setting.conv));
    KcpFINInfo finInfo = {
        .timeout = m_disconnectTimeout,
        .conv = spContext->m_setting.conv,
        .timerId = spTimer->getUniqueId(),
        .sn = timeS,
        .peer_addr = spContext->m_setting.remote_addr,
    };

    m_finDisconnectQueue.push_back(finInfo);
}

} // namespace eular

