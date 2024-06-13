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

namespace eular {
class Kcp
{
    friend class KcpManager;
    friend class KcpContext;

public:
    typedef std::shared_ptr<Kcp> SP;

    Kcp();
    virtual ~Kcp();

    /**
     * @brief 创建UDPsocket并绑定到指定地址
     * 
     * @param ip 
     * @param port 
     * @return true 
     * @return false 
     */
    virtual bool bind(const eular::String8 &ip, uint16_t port = 0) noexcept;
    void close() noexcept;
    void setAutoClose(bool autoClose) noexcept { m_autoClose = autoClose; }

    const String8&  getLocalHost() const;
    uint16_t        getLocalPort() const;

protected:
    virtual void onReadEvent() = 0;

protected:
    KcpManager*     m_pKcpManager;  // KcpManager对象
    String8         m_localHost;    // 本地绑定的IP
    uint16_t        m_localPort;    // 本地绑定的端口
    int32_t         m_updSocket;    // udp文件描述符
    bool            m_autoClose;    // 自动关闭socket
};

} // namespace eular
#endif // __KCP_FIBER_H__
