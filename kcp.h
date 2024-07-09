/*************************************************************************
    > File Name: Kcp.h
    > Author: hsz
    > Brief:
    > Created Time: Mon 04 Jul 2022 11:25:28 AM CST
 ************************************************************************/

#ifndef __KCP_H__
#define __KCP_H__

#include <stdint.h>
#include <utils/string8.h>

#include "kcp_context.h"

namespace eular {
class KcpManager;
using DisconnectEventCB = std::function<void(std::shared_ptr<KcpContext>)>;

class Kcp
{
    friend class KcpManager;
    friend class KcpContext;
public:
    typedef std::shared_ptr<Kcp> SP;

    Kcp();
    virtual ~Kcp();

    /**
     * @brief 创建UDPsocket并绑定到指定地址, 不指定端口时将随机绑定
     * 
     * @param ip 
     * @param port 
     * @return true 
     * @return false 
     */
    virtual bool bind(const eular::String8 &ip, uint16_t port = 0) noexcept;
    void close() noexcept;
    void setAutoClose(bool autoClose) noexcept { m_autoClose = autoClose; }

    /**
     * @brief 设置连接超时, 默认3000(ms)
     * 
     * @param timeout 超时时间(min:100,max:5000)
     */
    void setConnectTimeout(uint32_t timeout) noexcept;

    /**
     * @brief 设置连接超时, 默认3000(ms)
     * 
     * @param timeout 超时时间(min:100,max:5000)
     */
    void setDisconnectTimeout(uint32_t timeout) noexcept;

    const String8&  getLocalHost() const;
    uint16_t        getLocalPort() const;

protected:
    // 断连请求
    struct KcpFINInfo {
        uint32_t    timeout;    // 断连超时
        uint32_t    conv;       // 会话号
        uint64_t    timerId;    // 定时器Id
        uint32_t    sn;         // 序列号
        sockaddr_in peer_addr;  // 对端IP和端口
    };

protected:
    virtual void onReadEvent() = 0;
    virtual void setKcpManager(KcpManager* pKcpManager);

protected:
    KcpManager*     m_pKcpManager;          // KcpManager对象
    String8         m_localHost;            // 本地绑定的IP
    uint16_t        m_localPort;            // 本地绑定的端口
    int32_t         m_updSocket;            // udp文件描述符
    uint32_t        m_connectTimeout;       // 连接超时
    uint32_t        m_disconnectTimeout;    // 断连超时
    bool            m_autoClose;            // 自动关闭socket
};

} // namespace eular
#endif // __KCP_FIBER_H__
