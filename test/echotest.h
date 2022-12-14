#pragma once

#include <gtest/gtest.h>
#include <commlib/piper.h>
#include <atomic>
#include <deque>
#include <map>
#include <string>
#include <semaphore>
#include <limits>
#include <latch>
#include <iostream>
#include <random>

namespace echotest
{

using namespace uvcomms4;

/** Random strings source */
class RSource
{
public:
    std::string operator () ()
    {
        std::size_t len = mLengthGen(mRS);
        std::string result;
        result.reserve(len);
        for (std::size_t i = 0; i < len; i++)
            result.push_back((char)mCharGen(mRS));
        return result;
    }

private:
    std::mt19937    mRS{ std::random_device{}() };
    std::uniform_int_distribution<std::mt19937::result_type>
        mLengthGen{ 1, 128 * 1024 };
    std::uniform_int_distribution<std::mt19937::result_type>
        mCharGen{ 32, 127 };
};

//====================================================================================================
// SERVER DELEGATE
//====================================================================================================

 
class EchoServerDelegate: public PiperDelegate
{
public:
        void Startup(Piper * aPiper) override
        {
            mServer = aPiper;
            std::cout << "[EchoServer] Startup\n";
            startup_called = true;
        }

        void Shutdown() noexcept override
        {
            std::cout << "[EchoServer] Shutdown\n";
            shutdown_called = true;
        }

        void onNewConnection(Descriptor aListener, Descriptor aPipe) noexcept override
        {
            ++new_connection_count;
        }

        void onPipeClosed(Descriptor aPipe, int aErrCode) noexcept override
        {
            ++close_count;
            if(aErrCode) {
                ++closed_with_error_count;
                std::cerr << "Pipe error: " << aErrCode << std::endl;
            }
        }

        void onMessage(Descriptor aDescriptor, Collector & aCollector) noexcept override
        {
            // reminder: IO thread
            // we MUST extract the message here; otherwise, we'll have an infinite loop
            auto [status, message] = aCollector.getMessage<std::string>();
            if(status == CollectorStatus::HasMessage)
            {
                ++messages_received_count;
                mServer->write(aDescriptor, std::move(message), [this](int r){
                    if(r == 0)
                        ++messages_sent_count;
                    else {
                        std::cerr << "SVR: WRITE ERROR: " << r << std::endl;
                        ++write_errors_count;
                    }
                });
            }
        }

        bool    startup_called  { false };
        bool    shutdown_called { false };
        std::atomic<int> new_connection_count { 0 };
        std::atomic<int> close_count { 0 }; // must include the listener
        std::atomic<int> closed_with_error_count { 0 };
        std::atomic<int> messages_received_count { 0 };
        std::atomic<int> messages_sent_count { 0 };
        std::atomic<int> write_errors_count { 0 };

        void assess(std::size_t aTotalConnections, std::size_t aMessagesPerConnection)
        {
            EXPECT_TRUE(startup_called);
            EXPECT_TRUE(shutdown_called);
            EXPECT_EQ(new_connection_count, aTotalConnections);
            EXPECT_EQ(close_count, aTotalConnections + 1);
            EXPECT_EQ(closed_with_error_count, 0);
            EXPECT_EQ(messages_received_count, aTotalConnections * aMessagesPerConnection);
            EXPECT_EQ(messages_sent_count, aTotalConnections * aMessagesPerConnection);
            EXPECT_EQ(write_errors_count, 0);
        }

private:
    Piper   *mServer { nullptr };
};


//====================================================================================================
// CLIENT DELEGATE
//====================================================================================================

class EchoClientDelegate: PiperDelegate
    {
    public:
        EchoClientDelegate()
        {

        }

        void Startup(Piper * aPiper) override
        {

        }

        void Shutdown() noexcept override
        {

        }

        void onNewConnection(Descriptor aListener, Descriptor aPipe) noexcept override
        {

        }

        void onPipeClosed(Descriptor aPipe, int aErrCode) noexcept override
        {

        }

        void onMessage(Descriptor aDescriptor, Collector & aCollector) noexcept override
        {

        }

        // ===================================
    };

}