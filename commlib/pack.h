#pragma once

#include <cstdint>

namespace uvcomms4
{

/* The idea is to prefix every message with a header:
    4 bytes length (limited to 32bit), little-endian
    4 bytes length hash to make (almost) sure we didn't go out of sync
    if the hash does not match, then we should probably interrupt this connection as it is no longer healthy
*/

inline constexpr std::uint32_t length_hash(std::uint32_t aLength)
{
    auto a = aLength * aLength;
    a ^= (a << 13);
    a ^= (a >> 17);
    a ^= (a << 5);
    a ^= 0xAAAAAAAA;
    return a % 2147483647u;
}

template<typename T>
inline void u32_pack(std::uint32_t aValue, T *aDest)
{
    static_assert(sizeof(*aDest) = 1);
    aDest[0] = (aValue >> 0u ) & 0xFFu;
    aDest[1] = (aValue >> 8u ) & 0xFFu;
    aDest[2] = (aValue >> 16u) & 0xFFu;
    aDest[3] = (aValue >> 24u) & 0xFFu;
}

template<typename T>
inline std::uint32_t u32_unpack(T const *aSrc)
{
    return
        (((std::uint32_t)(std::uint8_t)aSrc[0]) << 0u)
      | (((std::uint32_t)(std::uint8_t)aSrc[1]) << 8u)
      | (((std::uint32_t)(std::uint8_t)aSrc[2]) << 16u)
      | (((std::uint32_t)(std::uint8_t)aSrc[3]) << 24u);
}


}