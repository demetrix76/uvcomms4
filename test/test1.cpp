#include <gtest/gtest.h>

#include "messagemock.h"
#include <commlib/pack.h>
#include <commlib/collector.h>
#include <cstring>
#include <string>

namespace mm = messagemock;
namespace u = uvcomms4;

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

TEST(MessageFormat, Collector1)
{
    using namespace std::literals;
    uvcomms4::Collector<std::string_view> collector;
    collector.append("ABCD"sv);
    collector.append("EFGH"sv);
    EXPECT_TRUE(collector.contains(8));
    EXPECT_TRUE(collector.contains(0));
    EXPECT_FALSE(collector.contains(9));

    std::string s1;
    EXPECT_TRUE(collector.copyTo(s1, 6, false));
    EXPECT_TRUE(s1 == "ABCDEF");

    std::string s2;
    EXPECT_TRUE(collector.copyTo(s2, 5, true));
    EXPECT_TRUE(s2 == "ABCDE");

    std::string s3;
    EXPECT_TRUE(collector.contains(3));
    EXPECT_FALSE(collector.contains(4));
    EXPECT_TRUE(collector.copyTo(s3, 3, true));
    EXPECT_TRUE(s3 == "FGH");
    EXPECT_TRUE(collector.contains(0));
    EXPECT_FALSE(collector.contains(1));

    collector.append("ABCD"sv);
    collector.append("EFGH"sv);

    std::string s4;
    EXPECT_FALSE(collector.copyTo(s4, 10, true));
    EXPECT_TRUE(s4 == "ABCDEFGH");

}

TEST(MessageFormat, Collector_Incomplete_length)
{
    mm::stream_t stream;
    mm::appendMessage(stream, "Message1");
    stream.resize(7);

    uvcomms4::Collector<mm::stream_t> collector;
    collector.append(std::move(stream));
    EXPECT_EQ(collector.messageLength(true), uvcomms4::MORE_DATA);
    EXPECT_TRUE(collector.contains(7));

}


TEST(MessageFormat, Collector_Corrupt)
{
    mm::stream_t stream;
    mm::appendMessage(stream, "Message1");
    stream[7] = '\xFF';
    uvcomms4::Collector<mm::stream_t> collector;
    collector.append(std::move(stream));
    EXPECT_EQ(collector.status(), uvcomms4::CollectorStatus::Corrupt);
    EXPECT_TRUE(collector.contains(8));
}


TEST(MessageFormat, Collector_MessageLength)
{
    mm::stream_t stream;
    mm::appendMessage(stream, "Message1234");

    uvcomms4::Collector<mm::stream_t> collector;
    collector.append(std::move(stream));
    EXPECT_EQ(collector.messageLength(true), 11);
    EXPECT_TRUE(collector.contains(11));
    EXPECT_FALSE(collector.contains(12));
}


TEST(MessageFormat, Collector_ExtractMessage)
{
    mm::stream_t stream;
    std::string msg1 = "Message1234";
    mm::appendMessage(stream, msg1);

    uvcomms4::Collector<mm::stream_t> collector;
    collector.append(std::move(stream));

    EXPECT_EQ(collector.status(), u::CollectorStatus::HasMessage);
    std::string emsg1;
    EXPECT_EQ(collector.extractMessageTo(emsg1), u::CollectorStatus::HasMessage);
    EXPECT_TRUE(msg1 == emsg1);
    EXPECT_EQ(collector.status(), u::CollectorStatus::NoMessage);

    mm::stream_t stream2;
    mm::appendMessage(stream2, msg1);
    collector.append(std::move(stream2));

    EXPECT_EQ(collector.status(), u::CollectorStatus::HasMessage);
    std::string emsg2;
    EXPECT_EQ(collector.extractMessageTo(std::back_inserter(emsg2)), u::CollectorStatus::HasMessage);
    EXPECT_TRUE(emsg2 == msg1);
    EXPECT_EQ(collector.status(), u::CollectorStatus::NoMessage);

}

TEST(MessageFormat, Collector_ExtractMessage2)
{
    mm::stream_t stream;
    std::string msg1 = "Message1234";
    std::string msg2 = "SomeOtherMessage";
    std::string msg3 = "";
    std::string msg4 = "OneMoreMessage";

    mm::appendMessage(stream, msg1);
    mm::appendMessage(stream, msg2);
    mm::appendMessage(stream, msg3);
    mm::appendMessage(stream, msg4);

    uvcomms4::Collector<mm::stream_t> collector;
    collector.append(std::move(stream));

    EXPECT_EQ(collector.status(), u::CollectorStatus::HasMessage);
    std::string emsg1;
    std::string emsg2;
    std::string emsg3;
    std::string emsg4;
    EXPECT_EQ(collector.extractMessageTo(emsg1), u::CollectorStatus::HasMessage);
    EXPECT_EQ(collector.extractMessageTo(emsg2), u::CollectorStatus::HasMessage);
    EXPECT_EQ(collector.extractMessageTo(emsg3), u::CollectorStatus::HasMessage);
    EXPECT_EQ(collector.extractMessageTo(emsg4), u::CollectorStatus::HasMessage);
    EXPECT_TRUE(msg1 == emsg1);
    EXPECT_TRUE(msg2 == emsg2);
    EXPECT_TRUE(msg3 == emsg3);
    EXPECT_TRUE(msg4 == emsg4);

}

TEST(MessageFormat, Collector_ExtractMessage_Split)
{
    mm::stream_t stream;
    std::string msg1 = "Message1234";      // 0: [8 bytes header]  8: [11 bytes message]
    std::string msg2 = "SomeOtherMessage"; //19: [8 bytes header] 27: [16 bytes message]
    std::string msg3 = "OneMoreMessage";  // 43: [8 bytes header] 51: [14 bytes message] (up to 65)

    mm::appendMessage(stream, msg1);
    mm::appendMessage(stream, msg2);
    mm::appendMessage(stream, msg3);
    EXPECT_EQ(stream.size(), 65);

    // making messages span across buffers:
    // buffer 0: [0-12), buffer boundary in message body
    // buffer 1: [12-22), buffer boundary in message header
    // buffer 2: [22-65), more than one message in buffer

    uvcomms4::Collector<std::string_view> collector;
    collector.append(std::string_view(&stream[0], 12));
    collector.append(std::string_view(&stream[12], 10));
    collector.append(std::string_view(&stream[22], 43));

    std::string emsg1;
    std::string emsg2;
    std::string emsg3;
    EXPECT_EQ(collector.extractMessageTo(emsg1), u::CollectorStatus::HasMessage);
    EXPECT_EQ(collector.extractMessageTo(emsg2), u::CollectorStatus::HasMessage);
    EXPECT_EQ(collector.extractMessageTo(emsg3), u::CollectorStatus::HasMessage);
    EXPECT_TRUE(msg1 == emsg1);
    EXPECT_TRUE(msg2 == emsg2);
    EXPECT_TRUE(msg3 == emsg3);
}


TEST(MessageFormat, Collector_GetMessage)
{
    mm::stream_t stream;
    std::string msg1 = "Message1234";
    std::string msg2 = "SomeOtherMessage";
    std::string msg3 = "";
    std::string msg4 = "OneMoreMessage";

    mm::appendMessage(stream, msg1);
    mm::appendMessage(stream, msg2);
    mm::appendMessage(stream, msg3);
    mm::appendMessage(stream, msg4);

    uvcomms4::Collector<mm::stream_t> collector;
    collector.append(std::move(stream));

    EXPECT_EQ(collector.status(), u::CollectorStatus::HasMessage);

    auto [st1, emsg1] = collector.getMessage<std::string>();
    auto [st2, emsg2] = collector.getMessage<std::string>();
    auto [st3, emsg3] = collector.getMessage<std::string>();
    auto [st4, emsg4] = collector.getMessage<std::string>();

    EXPECT_EQ(msg1, emsg1);
    EXPECT_EQ(msg2, emsg2);
    EXPECT_EQ(msg3, emsg3);
    EXPECT_EQ(msg4, emsg4);

}