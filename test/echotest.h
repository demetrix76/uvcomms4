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
                mServer->write(aDescriptor, std::move(message), [aDescriptor, this](int r){
                    if(r == 0)
                        ++messages_sent_count;
                    else {
                        std::cerr << "SVR: WRITE ERROR: " << r << std::endl;
                        ++write_errors_count;
                        // close the pipe
                        this->mServer->close(aDescriptor);
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

struct expectation
{
    std::deque<std::string> messages;
    int counter;
};


class EchoClientDelegate: public PiperDelegate
    {
    public:
        EchoClientDelegate(std::latch &aCompletionLatch):
            mCompletionLatch(aCompletionLatch)
        {}

        void Startup(Piper * aPiper) override
        {
            startup_called = true;
            mClient = aPiper;
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
            if(aErrCode < 0)
                ++closed_with_error_count;
            if(1 == mOpenPipesCount--)
                signalDone();
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
                    mClient->close(aDescriptor);
                }
            }
        }

        //===================================
        void spinUp(std::string const &aPipeName, std::size_t aConnectionsCount, std::size_t aMessagesPerConnection)
        {
            /* Each Client instance creates aConnectionsCount pipes; we may close it once all those pipes have been closed */
            /* Failed connection attempts (e.g. temporary ECONNREFUSED cause creation/closing of a new pipe)*/
            mOpenPipesCount = 1; // to prevent early 'done' signaling
            mDesiredConnectionsCount = (int)aConnectionsCount;
            mMessagesPerConnection = (int)aMessagesPerConnection;

            for(std::size_t i = 0; i < aConnectionsCount; i++)
            {
                auto [connectedPipe, errCode] = tryConnect(aPipeName);

                // simulate total refuse
                // if(i == 99)
                // {
                //     mClient->close(connectedPipe);
                //     errCode = UV_ECONNREFUSED;
                // }

                if(0 == errCode)
                {
                    ++successful_connections_count;
                    addExpectation(connectedPipe, aMessagesPerConnection);
                    sendRandomMessage(connectedPipe, aMessagesPerConnection);
                }
                else
                {
                    std::cerr << "FAILED TO CONNECT: " << errCode << std::endl;
                }
            }

            if(0 == --mOpenPipesCount)
            {   // if mOpenPipesCount was 1 at the moment of decrementing, then
                // there are no more active pipes;
                // if there were more, then onPipeClosed will signal 'done' when appropriate
                signalDone();
            }
        }

        std::tuple<Descriptor, int>
        tryConnect(std::string const &aPipeName)
        {
            Descriptor connectedPipe = 0;
            int errCode = 0;

            // up to 10 attempts with delay to work around temporary ECONNREFUSED failures
            for(int i = 0; i < 10; i++)
            {
                ++mOpenPipesCount; // this may be incorrect if uv could not gen an fd for the pipe
                std::tie(connectedPipe, errCode) = mClient->connect(aPipeName).get();
                if(!((errCode == UV_ECONNREFUSED) || (errCode == UV_EAGAIN)))
                    break;
                else
                {
                    std::cerr << "ERROR: ECONNREFUSED\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // give the server some time to prepare for a new incoming connection
                }
            }
            return { connectedPipe, errCode };
        }

        void signalDone()
        {
            mCompletionLatch.count_down();
        }

        void sendRandomMessage(Descriptor aDescriptor, int aCounter)
        {
            if(aCounter < 1)
                return;

            std::string message = "Some random message";
            addExpectedMessage(aDescriptor, message);
            mClient->write(aDescriptor, std::move(message), [=, this](int aErrCode){
                if(0 == aErrCode)
                {
                    ++messages_sent_count;
                    sendRandomMessage(aDescriptor, aCounter - 1);
                }
                else
                {
                    ++write_errors_count;
                    std::cerr << "CLIENT Write error: " << aErrCode << std::endl;
                    mClient->close(aDescriptor, [](auto){});
                }
            });
        }

        bool checkExpectedMessage(Descriptor aDescriptor, std::string const &aReceivedMessage)
        {
            std::string expected_message;
            bool done = false;
            {
                std::lock_guard lk(mMx);
                auto &expectation = mExpectations.at(aDescriptor);
                expected_message = std::move(expectation.messages.front());
                expectation.messages.pop_front();
                done = (0 == --expectation.counter);
            }
            if(done)
                mClient->close(aDescriptor, [](auto){});

            return expected_message == aReceivedMessage;
        }

        void addExpectedMessage(Descriptor aDescriptor, std::string const &aMessage)
        {
            std::lock_guard lk(mMx);
            auto &expect = mExpectations.at(aDescriptor);
            expect.messages.push_back(aMessage);
        }

        void addExpectation(Descriptor aDescriptor, int aCounter)
        {
            std::lock_guard lk(mMx);
            mExpectations.insert({aDescriptor, {{}, aCounter}});
        }

        void assess()
        {
            EXPECT_TRUE(startup_called);
            EXPECT_TRUE(shutdown_called);
            EXPECT_EQ(successful_connections_count, mDesiredConnectionsCount);
            EXPECT_EQ(messages_sent_count, mDesiredConnectionsCount * mMessagesPerConnection);
            EXPECT_EQ(messages_received_count, mDesiredConnectionsCount * mMessagesPerConnection);
            EXPECT_EQ(write_errors_count, 0);
            EXPECT_EQ(new_connections_count, 0);
            EXPECT_EQ(closed_with_error_count, 0);
            EXPECT_EQ(bad_messages_count, 0);
        }


        //statistics
        std::atomic<bool>   startup_called { false };
        std::atomic<bool>   shutdown_called { false };

        std::atomic<int>    successful_connections_count { 0 };
        std::atomic<int>    messages_sent_count { 0 };
        std::atomic<int>    messages_received_count { 0 };
        std::atomic<int>    write_errors_count { 0 };
        std::atomic<int>    new_connections_count { 0 };// should remain 0
        std::atomic<int>    closed_with_error_count { 0 };
        std::atomic<int>    bad_messages_count { 0 };

    private:
        Piper   *mClient { nullptr };
        std::latch          &mCompletionLatch;
        std::atomic<int>    mOpenPipesCount { 0 };
        std::mutex          mMx;
        std::map<Descriptor, expectation>
                            mExpectations;
        int                 mDesiredConnectionsCount { 0 };
        int                 mMessagesPerConnection { 0 };
    };

}