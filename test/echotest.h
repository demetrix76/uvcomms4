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

//using queue_t = std::deque<std::string>;
struct expectation
{
    std::deque<std::string> messages;
    int counter;
};

class EchoClientDelegate: public PiperDelegate
{
public:
    EchoClientDelegate(std::size_t aConnectionCount, std::latch &aCompletionLatch):
        mCompletionLatch(aCompletionLatch),
        mConnectionCount(aConnectionCount)
    {}

    void Startup(Piper * aPiper) override
    {
        mClient = aPiper;
        startup_called = true;
    }

    void Shutdown() noexcept override
    {
        shutdown_called = true;
    }

    void onNewConnection(Descriptor aListener, Descriptor aPipe) noexcept override
    {
        ++new_connections_count;
    }

    void onPipeClosed(Descriptor aPipe, int aErrCode) noexcept override
    {
        auto cc = ++close_count;
        if(aErrCode != 0)
            ++closed_with_error_count;

        if(cc == pipes_created_count)
            signalDone(); // this is not quite right - we may have extra close_count on repeated attempts to connect
    }

    void onMessage(Descriptor aDescriptor, Collector & aCollector) noexcept override
    {
        auto [status, message] = aCollector.getMessage<std::string>();
        if(status == CollectorStatus::HasMessage)
        {
            ++messages_received_count;
            if(!checkExpectedMessage(aDescriptor, message))
            {
                ++bad_messages_count;
                mClient->close(aDescriptor, [](auto){});
            }
        }
    }

    // ===============
    void addExpectedMessage(Descriptor aDescriptor, std::string const &aMessage)
    {
        std::lock_guard lk(mMx);
        auto &expectation = mExpectations.at(aDescriptor);
        expectation.messages.push_back(aMessage);

    }

    bool checkExpectedMessage(Descriptor aDescriptor, std::string const &aMessage)
    {
        std::string expected_mesage;
        bool done = false;
        {
            std::lock_guard lk(mMx);
            auto &expectation = mExpectations.at(aDescriptor);
            expected_mesage = std::move(expectation.messages.front());
            expectation.messages.pop_front();
            done = (0 == --expectation.counter);
        }

        if(done)
            mClient->close(aDescriptor, [](auto){});

        return expected_mesage == aMessage;
    }

    void addExpectation(Descriptor aDescriptor, int aCount)
    {
        std::lock_guard lk(mMx);
        mExpectations.insert({aDescriptor, {{}, aCount}});
    }

    void sendRandomMessage(Descriptor aDescriptor, int aRemaining)
    {
        if(aRemaining > 0)
        {
            std::string message = mRSource();// "Some random message";
            addExpectedMessage(aDescriptor, message);
            mClient->write(aDescriptor, std::move(message), [=, this](int aErrCode){
                if(0 == aErrCode)
                {
                    ++messages_sent_count;
                    sendRandomMessage(aDescriptor, aRemaining - 1);
                }
                else
                {
                    ++write_errors_count;
                    std::cerr << "WRITE ERROR: " << aErrCode << std::endl;
                    mClient->close(aDescriptor, [](auto){});
                }
            });
        } // N.B. too early to close; there will be a return message
    }

    void spinUp(std::string const & aPipeName, std::size_t aConnectionsCount, std::size_t aMessagesCount)
    {
        std::latch sync(aConnectionsCount);

        for(std::size_t i = 0; i < aConnectionsCount; i++)
        {
            Descriptor connectedPipe = 0;
            int errCode = 0;

            for(int i = 0; i < 100; i++)
            {
                auto [newPipe, err] = mClient->connect(aPipeName).get();
                ++pipes_created_count;
                connectedPipe = newPipe;
                errCode = err;
                if(err == UV_ECONNREFUSED)
                {
                    std::cerr << "UV_ECONNREFUSED\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                break;
            }

            if(0 == errCode)
            {
                ++successful_connections_count;
                addExpectation(connectedPipe, aMessagesCount);
                // start the first write; subsequent writes will be initiated by the completion handler
                sendRandomMessage(connectedPipe, aMessagesCount);
            }
            else
            {
                ++total_connections_count;
                std::cerr << "FAILED TO CONNECT: " << errCode << std::endl;
            }
        }

        if(0 == successful_connections_count)
        {
            //  there will be no onPipeClosed so we should end up early; tell the caller we're done here
            std::cerr << "No single connection attempt succeeded\n";
            //signalDone(); // all handles still get closed, so this signal confuses the system, making latch count negative
        }
            // mClient->connect(aPipeName, [&sync, aMessagesCount, this](auto c){
            //     auto [connectedPipe, errCode] = c;
            //     ++total_connections_count;
            //     if(0 == errCode)
            //     {
            //         ++successful_connections_count;
            //         addExpectation(connectedPipe, aMessagesCount);
            //         // start the first write; subsequent writes will be initiated by the completion handler
            //         sendRandomMessage(connectedPipe, aMessagesCount);
            //     }
            //     else // failed to connect
            //     {
            //         std::cerr << "FAILED TO CONNECT: " << errCode << std::endl;
            //         // do nothing
            //     }
            //     sync.count_down();
            // });

        // sync.wait(); // wait until all connection attepts are done
    }

    void signalDone()
    {
        done_signaled = true;
        mCompletionLatch.count_down();
    }

    void assess(std::size_t aConnectionsCount, std::size_t aMessagesCount)
    {
        EXPECT_TRUE(startup_called);
        EXPECT_TRUE(shutdown_called);
        EXPECT_EQ(close_count, aConnectionsCount);
        EXPECT_EQ(new_connections_count, 0);
        EXPECT_EQ(successful_connections_count, aConnectionsCount);
        EXPECT_EQ(closed_with_error_count, 0);
        EXPECT_EQ(messages_received_count, aConnectionsCount * aMessagesCount);
        EXPECT_EQ(bad_messages_count, 0);
        EXPECT_EQ(messages_sent_count, aConnectionsCount * aMessagesCount);
        EXPECT_EQ(write_errors_count, 0);
    }

    bool    done_signaled { false };
    bool    startup_called { false };
    bool    shutdown_called { false };


    std::atomic<std::size_t> new_connections_count { 0 }; // must remain at 0
    std::atomic<std::size_t> successful_connections_count { 0 }; // these are outgoing connections
    std::atomic<std::size_t> total_connections_count { 0 }; // these are outgoing connections
    std::atomic<std::size_t> close_count { 0 };
    std::atomic<std::size_t> closed_with_error_count { 0 };
    std::atomic<std::size_t> messages_received_count { 0 };
    std::atomic<std::size_t> bad_messages_count { 0 };
    std::atomic<std::size_t> messages_sent_count { 0 };
    std::atomic<std::size_t> write_errors_count { 0 };

    std::atomic<std::size_t> pipes_created_count { 0 };


private:
    Piper *mClient { nullptr };

    std::latch  &mCompletionLatch;
    std::size_t  mConnectionCount { 0 };

    std::mutex              mMx;
    std::map<Descriptor, expectation> mExpectations;

    RSource mRSource;

};

}