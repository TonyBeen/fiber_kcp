/*************************************************************************
    > File Name: kcpserver.h
    > Author: hsz
    > Brief:
    > Created Time: Fri 07 Jun 2024 06:00:34 PM CST
 ************************************************************************/

#ifndef __KCP_SERVER_H__
#define __KCP_SERVER_H__

#include <map>

#include <utils/bitmap.h>

#include "kcp.h"
#include "kcp_context.h"
#include "kcp_protocol.h"

namespace eular {
using ConnectEventCB = std::function<bool(std::shared_ptr<KcpContext>)>;

class KcpServer : public Kcp
{
    friend class KcpManager;
    friend class KcpContext;
public:
    typedef std::shared_ptr<KcpServer> SP;

    KcpServer();
    ~KcpServer();

    /**
     * @brief 注册连接事件(非线程安全)
     * 
     * @param connectEventCB 连接回调
     */
    void installConnectEvent(ConnectEventCB connectEventCB) noexcept;

    /**
     * @brief 注册断连事件(非线程安全)
     * 
     * @param disconnectEventCB 断连回调
     */
    void installDisconnectEvent(DisconnectEventCB disconnectEventCB) noexcept;

    /**
     * @brief 重置所有连接
     * 
     */
    void resetAll() noexcept;

protected:
    // 读事件
    void onReadEvent() override;

    // 会话数据
    void onKcpDataReceived(const ByteBuffer &buffer, uint32_t conv, sockaddr_in peerAddr);

    // 连接请求
    void onSYNReceived(protocol::KcpProtocol *pKcpProtocolReq, sockaddr_in peerAddr);

    // 确认断开连接请求
    void onACKReceived(protocol::KcpProtocol *pKcpProtocolReq, sockaddr_in peerAddr);

    // 请求断开连接
    void onFINReceived(protocol::KcpProtocol *pKcpProtocolReq, sockaddr_in peerAddr);

    // 重置请求
    void onRSTReceived(protocol::KcpProtocol *pKcpProtocolReq, sockaddr_in peerAddr);

    // 连接超时
    void onConnectTimeout(uint32_t conv);

    // 断连超时
    void onDisconnectTimeout(uint32_t conv);

    // 主动关闭context回调
    void onKcpContextClosed(KcpContext::SP spContext, bool isClose);

    // 半连接
    struct KcpSYNInfo {
        uint32_t    timeout;    // 连接超时
        uint32_t    conv;       // 会话号
        sockaddr_in peer_addr;  // 对端IP和端口
        uint64_t    timerId;    // 定时器Id
        protocol::KcpProtocol syn_protocol;
    };

private:
    BitMap                              m_kcpConvBitmap;        // 会话号位图
    ByteBuffer                          m_kcpBuffer;            // 存储KCP数据
    ConnectEventCB                      m_connectEventCB;       // 连接回调
    DisconnectEventCB                   m_disconnectEventCB;    // 断连回调
    std::list<KcpSYNInfo>               m_synConnectQueue;      // 半连接队列
    std::list<KcpFINInfo>               m_finDisconnectQueue;   // 半断连队列
    std::map<uint32_t, KcpContext::SP>  m_kcpContextMap;        // 会话号 -> KcpContext的映射
};
} // namespace eular
#endif // __KCP_SERVER_H__
