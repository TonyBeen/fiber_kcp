/*************************************************************************
    > File Name: kcp_client.h
    > Author: hsz
    > Brief:
    > Created Time: Fri 05 Jul 2024 09:15:16 AM CST
 ************************************************************************/

#ifndef __KCP_CLIENT_H__
#define __KCP_CLIENT_H__

#include <map>

#include <utils/bitmap.h>

#include "kcp.h"
#include "kcp_context.h"
#include "kcp_protocol.h"

namespace eular {
class KcpClient : public Kcp
{
public:
    typedef std::shared_ptr<KcpClient> SP;

    KcpClient();
    ~KcpClient();

    /**
     * @brief 设置发送窗口和接收窗口大小
     * 
     * @param sendWinSize 
     * @param recvWinSize 
     */
    void setWindowSize(uint32_t sendWinSize, uint32_t recvWinSize) noexcept;

    /**
     * @brief 连接到指定Kcp
     * 
     * @param host 对端IP
     * @param port 对端端口
     * @param mode kcp模式
     * @param timeout 超时时间(ms)
     * @return true 连接成功
     * @return false 连接失败(包含超时)
     */
    bool connect(const String8 &host, uint16_t port, KCPMode mode = KCPMode::Normal, uint32_t timeout = 1000) noexcept;

    /**
     * @brief 获取context实例, 用于发送接收
     * 
     * @return KcpContext::SP 
     */
    KcpContext::SP getContext() const { return m_context; }

    /**
     * @brief 注册断连事件(非线程安全)
     * 
     * @param disconnectEventCB 断连回调
     */
    void installDisconnectEvent(DisconnectEventCB disconnectEventCB) noexcept;

protected:
    // 读事件
    void onReadEvent() override;

    // 收到命令
    void onCommandReceived(protocol::KcpProtocol &kcpProtoInput);

    // 关闭事件
    void onContextClosed(KcpContext::SP spContext, bool isClose);

    // 断连超时
    void onDisconnectTimeout(uint32_t conv);

private:
    uint32_t            m_sendWinSize;
    uint32_t            m_recvWinSize;
    ByteBuffer          m_kcpBuffer;            // 存储KCP数据
    KcpContext::SP      m_context;
    DisconnectEventCB   m_disconnectEventCB;    // 断连回调
    KcpFINInfo          m_finInfo;
};

} // namespace eular

#endif // __KCP_CLIENT_H__
