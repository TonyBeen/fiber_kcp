/*************************************************************************
    > File Name: fiber.cpp
    > Author: hsz
    > Brief:
    > Created Time: Mon 04 Jul 2022 11:25:32 AM CST
 ************************************************************************/

#include "kcp.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <log/log.h>

#include "ikcp.h"
#include "kcp_protocol.h"

#define LOG_TAG "Kcp"

#define MIN_TIMEOUT 100
#define MAX_TIMEOUT 5000

namespace eular {
Kcp::Kcp() :
    m_pKcpManager(nullptr),
    m_updSocket(-1),
    m_connectTimeout(3000),
    m_disconnectTimeout(3000),
    m_autoClose(false)
{
}

Kcp::~Kcp()
{
    if (m_autoClose && m_updSocket > 0) {
        ::close(m_updSocket);
    }
}

bool Kcp::bind(const eular::String8 &ip, uint16_t port) noexcept
{
    if (m_updSocket > 0) {
        return true;
    }

    sockaddr_in localAddr;
    socklen_t len = sizeof(sockaddr_in);
    m_updSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_updSocket < 0) {
        LOGE("create udp socket error: [%d,%s]", errno, strerror(errno));
        return false;
    }

    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    localAddr.sin_port = htons(port);

    int32_t status = ::bind(m_updSocket, (struct sockaddr*)&localAddr, len);
    if (status < 0) {
        close();
        LOGE("udp socket bind [%s:%u] error: [%d,%s]", ip.c_str(), port, errno, strerror(errno));
        return false;
    }

    m_localHost = ip;
    m_localPort = port;
    if (port == 0) {
        status = getsockname(m_updSocket, (sockaddr *)&localAddr, &len);
        if (0 == status) {
            char ipv4Host[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &localAddr.sin_addr, ipv4Host, INET_ADDRSTRLEN);
            m_localHost = ipv4Host;
            m_localPort = ntohs(localAddr.sin_port);
        } else {
            LOGE("getsockname(%d) error. [%d, %s]", m_updSocket, errno, strerror(errno));
        }
    }

    LOGI("Successfully bound to [%s:%u]", ip.c_str(), port);
    return true;
}

void Kcp::close() noexcept
{
    if (m_updSocket > 0) {
        ::close(m_updSocket);
        m_updSocket = -1;
    }
}

void Kcp::setConnectTimeout(uint32_t timeout) noexcept
{
    if (timeout < MIN_TIMEOUT) {
        timeout = MIN_TIMEOUT;
    } else if (timeout > MAX_TIMEOUT) {
        timeout = MAX_TIMEOUT;
    }

    m_connectTimeout = timeout;
}

void Kcp::setDisconnectTimeout(uint32_t timeout) noexcept
{
    if (timeout < MIN_TIMEOUT) {
        timeout = MIN_TIMEOUT;
    } else if (timeout > MAX_TIMEOUT) {
        timeout = MAX_TIMEOUT;
    }

    m_disconnectTimeout = timeout;
}

const String8& Kcp::getLocalHost() const
{
    return m_localHost;
}

uint16_t Kcp::getLocalPort() const
{
    return m_localPort;
}

} // namespace eular
