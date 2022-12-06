#include <gtest/gtest.h>

#include <commlib/pack.h>
#include <cstring>

TEST(MessageFormat, PackUnpack)
{
    using namespace uvcomms4;

    // strcmp may succeed early so don't use 00 byte in test values
    char buf[5] {};
    u32_pack(0x7C332511, buf);
    EXPECT_EQ(0, std::strncmp(buf, "\x11\x25\x33\x7c", 4));
    EXPECT_EQ(0x7C332511, u32_unpack(buf));
    u32_pack(0xFA716E85, buf);
    EXPECT_EQ(0xFA716E85, u32_unpack(buf));
    EXPECT_EQ(0, std::strncmp(buf, "\x85\x6E\x71\xFA", 4));
    u32_pack(0x01020304, buf);
    EXPECT_EQ(0x01020304, u32_unpack(buf));
    EXPECT_EQ(0, std::strncmp(buf, "\x04\x03\x02\x01", 4));

}