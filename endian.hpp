/*************************************************************************
    > File Name: endian.hpp
    > Author: hsz
    > Brief:
    > Created Time: Fri 25 Mar 2022 09:32:43 AM CST
 ************************************************************************/

#ifndef __EULAR_ENDIAN_H__
#define __EULAR_ENDIAN_H__


#include <endian.h>     // for BYTE_ORDER
#include <byteswap.h>
#include <stdint.h>
#include <type_traits>  // for enable_if

#include <utils/sysdef.h>

namespace eular {
// 8字节类型转换
template<typename T>
typename std::enable_if<sizeof(T) == sizeof(uint64_t), T>::type
byteswap(T value)
{
    return (T)bswap_64((uint64_t)value);
}

// 4字节类型转换
template<typename T>
typename std::enable_if<sizeof(T) == sizeof(uint32_t), T>::type
byteswap(T value)
{
    return (T)bswap_32((uint32_t)value);
}

// 2字节类型转换
template<typename T>
typename std::enable_if<sizeof(T) == sizeof(uint16_t), T>::type
byteswap(T value)
{
    return (T)bswap_16((uint16_t)value);
}

#if BYTE_ORDER == BIG_ENDIAN
template<typename T>
T LittleEndian2BigEndian(T value)
{
    return value;
}

template<typename T>
T BigEndian2LittleEndian(T value)
{
    return byteswap(value);
}

#else

// 将value转换为大端字节数，在小端机执行byteswap
template<typename T>
T LittleEndian2BigEndian(T value)
{
    return byteswap(value);
}

// 将value转换为小端字节数，在小端机直接返回
template<typename T>
T BigEndian2LittleEndian(T value)
{
    return value;
}
#endif

} // namespace eular



#endif // __EULAR_ENDIAN_H__