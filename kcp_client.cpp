/*************************************************************************
    > File Name: kcp_client.cpp
    > Author: hsz
    > Brief:
    > Created Time: Fri 05 Jul 2024 09:25:49 AM CST
 ************************************************************************/

#include "kcp_client.h"

#include <log/log.h>
#include <utils/elapsed_time.h>

#include "ikcp.h"
#include "kcp_manager.h"
#include "kcp_protocol.h"
#include "kcp_utils.h"

#define LOG_TAG "KcpClient"

namespace eular {
KcpClient::KcpClient() :
    m_sendWinSize(DEFAULT_WIN_SIZE),
    m_recvWinSize(DEFAULT_WIN_SIZE),
    m_kcpBuffer(2 * MTU_SIZE)
{
}

KcpClient::~KcpClient()
{
}

void KcpClient::setWindowSize(uint32_t sendWinSize, uint32_t recvWinSize) noexcept
{
    if (sendWinSize > 0) {
        m_sendWinSize = sendWinSize;
    }

    if (recvWinSize > 0) {
        m_recvWinSize = recvWinSize;
    }
}

bool KcpClient::connect(const String8 &host, uint16_t port, KCPMode mode, uint32_t timeout) noexcept
{
    if (m_updSocket < 0) {
        return false;
    }

    if (m_context != nullptr) {
        return true;
    }

    sockaddr_in recvAddr;
    sockaddr_in peerAddr;
    socklen_t addrLen = sizeof(peerAddr);

    peerAddr.sin_family = AF_INET;
    peerAddr.sin_addr.s_addr = inet_addr(host.c_str());
    peerAddr.sin_port = htons(port);

    protocol::KcpProtocol kcpProtoOutput;
    protocol::KcpProtocol kcpProtoInput;
    protocol::InitKcpProtocol(&kcpProtoOutput);
    uint8_t bufProtoOutput[protocol::KCP_PROTOCOL_SIZE] = {0};
    kcpProtoOutput.kcp_mode = mode;
    kcpProtoOutput.syn_command = protocol::SYNCommand::SYN;
    kcpProtoOutput.sn = static_cast<uint32_t>(::time(NULL));
    kcpProtoOutput.send_win_size = m_sendWinSize;
    kcpProtoOutput.recv_win_size = m_recvWinSize;
    protocol::SerializeKcpProtocol(&kcpProtoOutput, bufProtoOutput);

    // 发送数据
    ::sendto(m_updSocket, bufProtoOutput, protocol::KCP_PROTOCOL_SIZE, 0, (sockaddr *)&peerAddr, addrLen);

    do {
        fd_set fdSet;
        FD_ZERO(&fdSet);
        FD_SET(m_updSocket, &fdSet);
        struct timeval tv;
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = timeout % 1000 * 1000;
        int32_t status = ::select(m_updSocket + 1, nullptr, &fdSet, nullptr, &tv);
        if (status < 0) {
            LOGE("Failed to select: [%d:%s]", errno, strerror(errno));
            break;
        } else if (status == 0) {
            LOGW("Connection timed out");
            break;
        }

        // 读取内容
        status = ::recvfrom(m_updSocket, bufProtoOutput, protocol::KCP_PROTOCOL_SIZE, 0, (sockaddr *)&recvAddr, &addrLen);
        if (status < 0) {
            LOGE("Recvfrom(%d) error: [%d:%s]", m_updSocket, errno, strerror(errno));
            break;
        }

        // 数据长度不满足指定字节数
        if (status != protocol::KCP_PROTOCOL_SIZE) {
            LOGW("Error response from %s", utils::Address2String((sockaddr *)&recvAddr));
            break;
        }

        // 未接收连接请求
        protocol::DeserializeKcpProtocol(bufProtoOutput, &kcpProtoInput);
        if (kcpProtoInput.syn_command != protocol::SYNCommand::ACK) {
            LOGW("Connection not permitted");
            break;
        }

        // 序列号不匹配
        if (kcpProtoOutput.sn != kcpProtoInput.sn) {
            LOGW("Inconsistent serial numbers");
            break;
        }

        LOGI("Connect success. Received from %s", utils::Address2String((sockaddr *)&recvAddr));
        KcpSetting setting;
        init_kcp(mode, &setting);
        setting.fd = m_updSocket;
        setting.conv = kcpProtoInput.kcp_conv;
        setting.send_wnd_size = m_sendWinSize;
        setting.recv_wnd_size = m_recvWinSize;
        setting.remote_addr = recvAddr;
        m_context = std::make_shared<KcpContext>();
        m_context->setSetting(setting);
        m_context->m_closeEvent = std::bind(&KcpClient::onContextClosed, this, std::placeholders::_1, std::placeholders::_2);

        return true;
    } while (false);

    return false;
}

void KcpClient::installDisconnectEvent(DisconnectEventCB disconnectEventCB) noexcept
{
    m_disconnectEventCB = disconnectEventCB;
}

void KcpClient::onReadEvent()
{
    static_assert(KCP_HEADER_SIZE == protocol::KCP_PROTOCOL_SIZE, "KCP_HEADER_SIZE != protocol::KCP_PROTOCOL_SIZE");

    sockaddr_in peerAddr;
    socklen_t len = sizeof(sockaddr_in);
    m_kcpBuffer.clear();

    // NOTE 由于连接成功时还未将Kcp加入到Manager, 导致连接成功无法添加定时器, 故挪到此处
    if (m_context->m_timerId == 0) {
        auto spTimer = m_pKcpManager->addTimer(m_context->m_setting.interval,
                std::bind(&KcpContext::onUpdateTimeout, m_context.get()), m_context->m_setting.interval);
        m_context->m_timerId = spTimer->getUniqueId();

        // 主动刷新一次
        m_context->onUpdateTimeout();
    }

    do {
#ifdef _DEBUG
        ElapsedTime stopwatch(ElapsedTimeType::NANOSECOND);
        stopwatch.start();
#endif
        int32_t realReadSize = TEMP_FAILURE_RETRY(::recvfrom(m_updSocket, m_kcpBuffer.data(), m_kcpBuffer.capacity(), 0,
                                                            (sockaddr *)&peerAddr, &len));
#ifdef _DEBUG
        stopwatch.stop();
        LOGI("recvfrom elapsed %zu us", stopwatch.elapsedTime());
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

        m_kcpBuffer.resize(realReadSize);
        uint8_t *pHeaderBuf = m_kcpBuffer.data();
        // 解析协议
        protocol::KcpProtocol kcpProtoInput;
        protocol::DeserializeKcpProtocol(pHeaderBuf, &kcpProtoInput);
        LOGI("kcp conv = %#x", kcpProtoInput.kcp_conv);
        if ((kcpProtoInput.kcp_flag & KCP_FLAG) != KCP_FLAG) {
            LOGW("Received a buffer without KCP_FALG(%#x) %#x", KCP_FLAG, kcpProtoInput.kcp_conv);
            m_kcpBuffer.clear();
            continue;
        }

        if (kcpProtoInput.kcp_flag == KCP_FLAG) {
            onCommandReceived(kcpProtoInput);
        } else if ((kcpProtoInput.kcp_flag & KCP_FLAG) == KCP_FLAG) {
            sockaddr *pRecvAddr = (sockaddr *)&peerAddr;
            sockaddr *pConnectedAddr = (sockaddr *)&m_context->m_setting.remote_addr;
            if (eular_unlikely(!utils::SockaddrEqual(pRecvAddr, pConnectedAddr))) {
                LOGW("received from [%s] is different to [%s]", utils::Address2String(pRecvAddr),
                    utils::Address2String(pConnectedAddr));
            } else {
                m_context->onRecv(m_kcpBuffer);
            }
        }
    } while (true);
}

void KcpClient::onCommandReceived(protocol::KcpProtocol &kcpProtoInput)
{
    switch (kcpProtoInput.syn_command) {
    case protocol::SYNCommand::ACK:
    {
        KcpManager::GetCurrentKcpManager()->delTimer(m_finInfo.timerId);
        break;
    }
    case protocol::SYNCommand::FIN:
    {
        protocol::KcpProtocol kcpProtoOutput;
        protocol::InitKcpProtocol(&kcpProtoOutput);
        kcpProtoOutput.kcp_conv = m_context->m_setting.conv;
        kcpProtoOutput.sn = kcpProtoInput.sn;
        kcpProtoOutput.syn_command = protocol::SYNCommand::ACK;

        uint8_t bufProtoOutput[KCP_HEADER_SIZE] = {0};
        protocol::SerializeKcpProtocol(&kcpProtoOutput, bufProtoOutput);

        ::sendto(m_updSocket, bufProtoOutput, KCP_HEADER_SIZE, 0,
            (sockaddr *)&(m_context->m_setting.remote_addr), sizeof(sockaddr_in));
        break;
    }
    case protocol::SYNCommand::RST:
        break;
    default:
        break;
    }

    KcpManager::GetCurrentKcpManager()->delTimer(m_context->m_timerId);
    if (m_disconnectEventCB) {
        m_disconnectEventCB(m_context);
    }
}

void KcpClient::onContextClosed(KcpContext::SP spContext, bool isClose)
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
            std::bind(&KcpClient::onDisconnectTimeout, this, spContext->m_setting.conv));
        KcpFINInfo finInfo = {
            .timeout = m_disconnectTimeout,
            .conv = spContext->m_setting.conv,
            .timerId = spTimer->getUniqueId(),
            .sn = timeS,
            .peer_addr = spContext->m_setting.remote_addr,
        };
        m_finInfo = finInfo;
    } else {
        // RST
        kcpProtoOutput.kcp_flag = KCP_FLAG;
        kcpProtoOutput.syn_command = protocol::SYNCommand::RST;
        kcpProtoOutput.sn = timeS;
        kcpProtoOutput.kcp_conv = spContext->m_setting.conv;
        protocol::SerializeKcpProtocol(&kcpProtoOutput, buffer);

        ::sendto(m_updSocket, buffer, protocol::KCP_PROTOCOL_SIZE, 0,
            (sockaddr *)&spContext->m_setting.remote_addr, sizeof(sockaddr_in));

        KcpManager::GetCurrentKcpManager()->delTimer(m_context->m_timerId);
        if (m_disconnectEventCB) {
            m_disconnectEventCB(spContext);
        }
    }
}

void KcpClient::onDisconnectTimeout(uint32_t conv)
{
    KcpManager::GetCurrentKcpManager()->delTimer(m_context->m_timerId);

    if (m_disconnectEventCB) {
        m_disconnectEventCB(m_context);
    }
}

} // namespace eular
