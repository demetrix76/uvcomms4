#pragma once

#include <commlib/pack.h>
#include <vector>
#include <string>
#include <algorithm>
#include <iterator>

namespace messagemock
{

using stream_t = std::vector<char>;

void appendMessage(stream_t &aStream, std::string_view aMessage)
{
    std::uint32_t msglen = static_cast<std::uint32_t>(aMessage.size());
    std::uint32_t lenhash = uvcomms4::length_hash(msglen);
    char buf[8];
    uvcomms4::u32_pack(msglen, buf);
    uvcomms4::u32_pack(lenhash, buf + 4);
    std::copy(buf, buf + std::size(buf), std::back_inserter(aStream));
    std::copy(aMessage.begin(), aMessage.end(), std::back_inserter(aStream));
}


}