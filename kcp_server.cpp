/*************************************************************************
    > File Name: kcpserver.cpp
    > Author: hsz
    > Brief:
    > Created Time: Fri 07 Jun 2024 06:00:37 PM CST
 ************************************************************************/

#include "kcp_server.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <utils/utils.h>
#include <utils/exception.h>
#include <utils/elapsed_time.h>
#include <log/log.h>

#include "ikcp.h"
#include "kcp_manager.h"
#include "kcp_utils.h"

#define LOG_TAG "KcpServer"

namespace eular {
KcpServer::KcpServer() :
    m_kcpBuffer(2 * MTU_SIZE)
{
    m_kcpConvBitmap.reserve(KCP_MAX_CONV);
    m_kcpConvBitmap.set(0, true);
}

KcpServer::~KcpServer()
{
}

void KcpServer::installConnectEvent(ConnectEventCB connectEventCB) noexcept
{
    m_connectEventCB = connectEventCB;
}

void KcpServer::installDisconnectEvent(DisconnectEventCB disconnectEventCB) noexcept
{
    m_disconnectEventCB = disconnectEventCB;
}

void KcpServer::resetAll() noexcept
{
    auto it = m_kcpContextMap.begin();
    do {
        it->second->resetContext();
        // NOTE resetContext回调会调用m_kcpContextMap.erase()函数, 故需要更新当前迭代器
        it = m_kcpContextMap.begin();
    } while (it != m_kcpContextMap.end());
}

void KcpServer::onReadEvent()
{
    LOGD("%s <begin>", __PRETTY_FUNCTION__);
    static_assert(KCP_HEADER_SIZE == protocol::KCP_PROTOCOL_SIZE);

    sockaddr_in peerAddr;
    socklen_t len = sizeof(sockaddr_in);

    // uint32_t canReadSize = 0;
    // ioctl(m_updSocket, FIONREAD, &canReadSize);

    // NOTE recvfrom在接收时按包收, UDP包最大长度为MTU, 故KCP一包长度为MTU
    m_kcpBuffer.clear();
    do {
#ifdef _DEBUG
        ElapsedTime stopwatch(ElapsedTimeType::NANOSECOND);
        stopwatch.start();
#endif
        int32_t realReadSize = TEMP_FAILURE_RETRY(::recvfrom(m_updSocket, m_kcpBuffer.data(), m_kcpBuffer.capacity(), 0,
                                                            (sockaddr *)&peerAddr, &len));
#ifdef _DEBUG
        stopwatch.stop();
        LOGD("recvfrom elapsed %zu us", stopwatch.elapsedTime());
#endif
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

        LOGD("recvfrom [%s] size = %d", utils::Address2String((sockaddr *)&peerAddr), realReadSize);
        m_kcpBuffer.resize(realReadSize);
        uint8_t *pHeaderBuf = m_kcpBuffer.data();

        // 解析协议
        protocol::KcpProtocol kcpProtoInput;
        protocol::DeserializeKcpProtocol(pHeaderBuf, &kcpProtoInput);
        LOGD("kcp conv = %#x", kcpProtoInput.kcp_flag);
        if ((kcpProtoInput.kcp_flag & KCP_FLAG) != KCP_FLAG) {
            LOGW("Received a buffer without KCP_FALG(%#x) %#x", KCP_FLAG, kcpProtoInput.kcp_flag);
            m_kcpBuffer.clear();
            continue;
        }

        if (kcpProtoInput.kcp_flag == KCP_FLAG) {
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
                onRSTReceived(&kcpProtoInput, peerAddr);
                break;
            default:
                break;
            }
        } else if ((kcpProtoInput.kcp_flag & KCP_FLAG) == KCP_FLAG) {
            // 处理KCP数据
            onKcpDataReceived(m_kcpBuffer, kcpProtoInput.kcp_flag, peerAddr);
        }

        m_kcpBuffer.clear();
    } while (true);
    LOGD("%s <end>", __PRETTY_FUNCTION__);
}

void KcpServer::onKcpDataReceived(const ByteBuffer &buffer, uint32_t conv, sockaddr_in peerAddr)
{
    // 如果从半连接队列找到此会话, 关闭定时器并添加到Map
    for (auto synIt = m_synConnectQueue.begin(); synIt != m_synConnectQueue.end(); ++synIt) {
        if (synIt->conv == conv) {
            KcpManager::GetCurrentKcpManager()->delTimer(synIt->timerId);
            KcpContext::SP spContext = std::make_shared<KcpContext>();
            KcpSetting setting;
            init_kcp(synIt->syn_protocol.kcp_mode, &setting);

            setting.fd = m_updSocket;
            setting.conv = conv;
            setting.send_wnd_size = synIt->syn_protocol.send_win_size;
            setting.recv_wnd_size = synIt->syn_protocol.recv_win_size;
            setting.remote_addr = peerAddr;
            spContext->setSetting(setting);
            spContext->m_localHost = m_localHost;
            spContext->m_localPort = m_localPort;
            spContext->m_closeEvent = std::bind(&KcpServer::onKcpContextClosed, this,
                                                std::placeholders::_1, std::placeholders::_2);

            if (m_connectEventCB && m_connectEventCB(spContext)) {
                auto spTimer = KcpManager::GetCurrentKcpManager()->addTimer(setting.interval,
                    std::bind(&KcpContext::onUpdateTimeout, spContext.get()), setting.interval);
                spContext->m_timerId = spTimer->getUniqueId();
                m_kcpContextMap[conv] = spContext;
            }

            m_synConnectQueue.erase(synIt);
            break;
        }
    }

    // 处理KCP数据
    auto ctxIt = m_kcpContextMap.find(conv);
    if (ctxIt != m_kcpContextMap.end()) {
        ctxIt->second->onRecv(buffer);
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
            break;
        }
    }

    communicationNo |= KCP_FLAG;

    protocol::KcpProtocol kcpProtoOutput;
    protocol::InitKcpProtocol(&kcpProtoOutput);
    uint8_t kcpProtoBuffer[protocol::KCP_PROTOCOL_SIZE] = {0};

    if (communicationNo == KCP_FLAG) {
        kcpProtoOutput.kcp_flag = KCP_FLAG;
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

    kcpProtoOutput.kcp_flag = KCP_FLAG;
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
    KcpFINInfo finInfo;
    finInfo.conv = 0;
    finInfo.timerId = 0;

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

        if (m_disconnectEventCB) {
            m_disconnectEventCB(it->second);
        }
        m_kcpContextMap.erase(it);
    }
}

// 请求断开连接
void KcpServer::onFINReceived(protocol::KcpProtocol *pKcpProtocolReq, sockaddr_in peerAddr)
{
    LOGD("%s FIN Received from %s", __PRETTY_FUNCTION__, utils::Address2String((sockaddr *)&peerAddr));
    uint32_t conv = pKcpProtocolReq->kcp_conv;
    protocol::KcpProtocol kcpProtoOutput;
    protocol::InitKcpProtocol(&kcpProtoOutput);
    uint8_t kcpProtoBuffer[protocol::KCP_PROTOCOL_SIZE] = {0};

    kcpProtoOutput.kcp_flag = KCP_FLAG;
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

        if (m_disconnectEventCB) {
            m_disconnectEventCB(it->second);
        }
        m_kcpContextMap.erase(it);
    }

    ::sendto(m_updSocket, kcpProtoBuffer, KCP_HEADER_SIZE, 0, (sockaddr *)&peerAddr, sizeof(peerAddr));
}

// 重置请求
void KcpServer::onRSTReceived(protocol::KcpProtocol *pKcpProtocolReq, sockaddr_in peerAddr)
{
    LOGD("%s RST Received from %s", __PRETTY_FUNCTION__, utils::Address2String((sockaddr *)&peerAddr));
    uint32_t conv = pKcpProtocolReq->kcp_conv;
    auto it = m_kcpContextMap.find(conv);
    if (it != m_kcpContextMap.end()) {
        KcpManager::GetCurrentKcpManager()->delTimer(it->second->m_timerId);

        if (m_disconnectEventCB) {
            m_disconnectEventCB(it->second);
        }
        m_kcpContextMap.erase(it);
    }
}

void KcpServer::onConnectTimeout(uint32_t conv)
{
    LOGD("%s conv = %u", __PRETTY_FUNCTION__, conv);
    // 超时将其从半连接队列移除并发送RST命令
    for (auto it = m_synConnectQueue.begin(); it != m_synConnectQueue.end(); ++it) {
        if (it->conv == conv) {
            // 将会话号加入备选
            uint32_t index = conv & ~KCP_MASK;
            m_kcpConvBitmap.set(index, false);

            protocol::KcpProtocol kcpProtoOutput;
            protocol::InitKcpProtocol(&kcpProtoOutput);
            kcpProtoOutput.syn_command = protocol::SYNCommand::RST;
            kcpProtoOutput.kcp_conv = it->conv;

            uint8_t bufProtoOutput[KCP_HEADER_SIZE] = {0};
            protocol::SerializeKcpProtocol(&kcpProtoOutput, bufProtoOutput);
            ::sendto(m_updSocket, bufProtoOutput, KCP_HEADER_SIZE, 0,
                    (sockaddr *)&(it->peer_addr), (socklen_t)sizeof(sockaddr_in));

            m_synConnectQueue.erase(it);
            break;
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

void KcpServer::onKcpContextClosed(KcpContext::SP spContext, bool isClose)
{
    // 主动关闭Context需要发送FIN
    protocol::KcpProtocol kcpProtoOutput;
    protocol::InitKcpProtocol(&kcpProtoOutput);

    // 以时间戳作为序列号
    uint32_t timeS = static_cast<uint32_t>(time(NULL));
    uint8_t buffer[protocol::KCP_PROTOCOL_SIZE] = {0};

    if (isClose) {
        kcpProtoOutput.kcp_flag = KCP_FLAG;
        kcpProtoOutput.syn_command = protocol::SYNCommand::FIN;
        kcpProtoOutput.sn = timeS;
        kcpProtoOutput.kcp_conv = spContext->m_setting.conv;

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
    } else {
        // RST
        kcpProtoOutput.kcp_flag = KCP_FLAG;
        kcpProtoOutput.syn_command = protocol::SYNCommand::RST;
        kcpProtoOutput.sn = timeS;
        kcpProtoOutput.kcp_conv = spContext->m_setting.conv;
        protocol::SerializeKcpProtocol(&kcpProtoOutput, buffer);

        ::sendto(m_updSocket, buffer, protocol::KCP_PROTOCOL_SIZE, 0,
            (sockaddr *)&spContext->m_setting.remote_addr, sizeof(sockaddr_in));

        auto it = m_kcpContextMap.find(kcpProtoOutput.kcp_conv);
        if (it != m_kcpContextMap.end()) {
            KcpManager::GetCurrentKcpManager()->delTimer(it->second->m_timerId);
            if (m_disconnectEventCB) {
                m_disconnectEventCB(it->second);
            }
            m_kcpContextMap.erase(it);
        }
    }
}

} // namespace eular

