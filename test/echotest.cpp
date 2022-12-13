#include <gtest/gtest.h>

#include "echotest.h"
#include <commlib/pack.h>
#include <commlib/collector.h>
#include <list>
#include <iterator>
#include <algorithm>
#include <thread>

using namespace uvcomms4;

// meant to be run on a separate thread, returning a list of delegates for assession

void clientWorker(std::size_t aClientCount, std::size_t aConnectionsPerClient, std::size_t aMessagesPerConnection)
{

}

TEST(EchoTest, EchoTest1)
{
    Config const &cfg = Config::get_default();
    ensure_socket_directory_exists(cfg);
    delete_socket_file(cfg);
    std::string pipename = pipe_name(cfg);

    auto server_delegate = std::make_shared<echotest::EchoServerDelegate>();
    {
        Piper server(server_delegate);
        auto [listener, errCode] = server.listen(pipename).get();
        EXPECT_EQ(errCode, 0);

        std::size_t client_count = 10;
        std::latch completionLatch(client_count);


        std::list<std::shared_ptr<echotest::EchoClientDelegate>> delegates;

        for(std::size_t i = 0; i < client_count; i++)
            delegates.emplace_back(std::make_shared<echotest::EchoClientDelegate>(completionLatch));

        {
            std::list<Piper> pipers;
            for(auto delegate: delegates)
                pipers.emplace_back(delegate);

            for(auto delegate: delegates)
                delegate->spinUp(pipename, 10, 100);

            completionLatch.wait();
        }



        for(auto delegate: delegates)
            delegate->assess(10, 100);
    }

    server_delegate->assess(10 * 10, 100);

}