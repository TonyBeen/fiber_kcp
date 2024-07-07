/*************************************************************************
    > File Name: kcp_utils.cpp
    > Author: hsz
    > Brief:
    > Created Time: Thu 13 Jun 2024 06:35:38 PM CST
 ************************************************************************/

#include "kcp_utils.h"

#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define INET_LEN 64

namespace utils {
const char *Address2String(const sockaddr *addr)
{
    static thread_local char formatAddr[INET_LEN] = {0};

    if (addr->sa_family == AF_INET) {
        sockaddr_in *ipv4Addr = (sockaddr_in *)addr;
        char ipv4Host[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &ipv4Addr->sin_addr, ipv4Host, INET_ADDRSTRLEN);

        snprintf(formatAddr, INET_LEN, "%s:%d", ipv4Host, ntohs(ipv4Addr->sin_port));
    } else if (addr->sa_family == AF_INET6) {
        sockaddr_in6 *ipv6Addr = (sockaddr_in6 *)addr;
        char ipv6Host[INET6_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET6, &ipv6Addr->sin6_addr, ipv6Host, INET6_ADDRSTRLEN);

        snprintf(formatAddr, INET_LEN, "[%s]:%d", ipv6Host, ntohs(ipv6Addr->sin6_port));
    }

    return formatAddr;
}

bool SockaddrEqual(const sockaddr *left, const sockaddr *right)
{
    if (left->sa_family != right->sa_family) {
        return false;
    }

    switch (left->sa_family) {
    case AF_INET:
    {
        const sockaddr_in *pLeft = (const sockaddr_in *)left;
        const sockaddr_in *pRight = (const sockaddr_in *)right;
        return (pLeft->sin_addr.s_addr == pRight->sin_addr.s_addr) && (pLeft->sin_port == pRight->sin_port);
    }
    case AF_INET6:
    {
        const sockaddr_in6 *pLeft = (const sockaddr_in6 *)left;
        const sockaddr_in6 *pRight = (const sockaddr_in6 *)right;

        bool hostEqual = 0 == memcmp(&pLeft->sin6_addr, &pRight->sin6_addr, sizeof(struct in6_addr));
        return hostEqual && pLeft->sin6_port == pRight->sin6_port;
    }
    default:
        break;
    }

    return false;
}

} // namespace utils
