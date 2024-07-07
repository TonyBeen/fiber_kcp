/*************************************************************************
    > File Name: kcp_utils.h
    > Author: hsz
    > Brief:
    > Created Time: Thu 13 Jun 2024 06:35:34 PM CST
 ************************************************************************/

#ifndef __KCP_UTILS_H__
#define __KCP_UTILS_H__

#include <stdint.h>

struct sockaddr;

namespace utils {

const char *Address2String(const sockaddr *addr);

bool SockaddrEqual(const sockaddr *left, const sockaddr *right);

} // namespace utils

#endif // __KCP_UTILS_H__
