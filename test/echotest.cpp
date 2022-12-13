#include <gtest/gtest.h>

#include "echotest.h"
#include <commlib/pack.h>
#include <commlib/collector.h>
#include <list>
#include <iterator>
#include <algorithm>
#include <thread>
#include <future>

using namespace uvcomms4;

// meant to be run on a separate thread, returning a list of delegates for assession

std::list<std::shared_ptr<echotest::EchoClientDelegate>>
clientWorker(std::string aPipeName, std::size_t aClientCount,
    std::size_t aConnectionsPerClient, std::size_t aMessagesPerConnection)
{
    std::list<std::shared_ptr<echotest::EchoClientDelegate>> delegates;

    std::latch completionLatch(aClientCount);

    for(std::size_t i = 0; i < aClientCount; i++)
        delegates.emplace_back(std::make_shared<echotest::EchoClientDelegate>(completionLatch));

    {
        std::list<Piper> pipers;
        for(auto delegate: delegates)
            pipers.emplace_back(delegate);

        for(auto delegate: delegates)
            delegate->spinUp(aPipeName, aConnectionsPerClient, aMessagesPerConnection);

        completionLatch.wait();
    }

    return delegates;
}

TEST(EchoTest, EchoTest1)
{
    Config const &cfg = Config::get_default();
    ensure_socket_directory_exists(cfg);
    delete_socket_file(cfg);
    std::string pipename = pipe_name(cfg);

    std::size_t workers_count = 10;
    std::size_t clients_per_worker = 10;
    std::size_t connections_per_client = 10;
    std::size_t messages_per_connection = 1000;

    auto server_delegate = std::make_shared<echotest::EchoServerDelegate>();
    {
        Piper server(server_delegate);
        auto [listener, errCode] = server.listen(pipename).get();
        EXPECT_EQ(errCode, 0);

        using worker_result = std::list<std::shared_ptr<echotest::EchoClientDelegate>>;

        std::list<std::future<worker_result>> wks;
        for(std::size_t i = 0; i < workers_count; i++)
        {
            wks.emplace_back(std::async(std::launch::async, [=]{
                return clientWorker(pipename, clients_per_worker,
                    connections_per_client, messages_per_connection);
            }));
        }

        std::list<worker_result> results;
        for(auto &wk: wks)
            results.emplace_back(wk.get());

        for(auto &result: results)
        {
            for(auto &delegate: result)
            {
                delegate->assess(connections_per_client, messages_per_connection);
            }
        }


        // std::size_t client_count = 10;
        // std::latch completionLatch(client_count);

        // std::list<std::shared_ptr<echotest::EchoClientDelegate>> delegates;

        // for(std::size_t i = 0; i < client_count; i++)
        //     delegates.emplace_back(std::make_shared<echotest::EchoClientDelegate>(completionLatch));

        // {
        //     std::list<Piper> pipers;
        //     for(auto delegate: delegates)
        //         pipers.emplace_back(delegate);

        //     for(auto delegate: delegates)
        //         delegate->spinUp(pipename, 10, 100);

        //     completionLatch.wait();
        // }



        // for(auto delegate: delegates)
        //     delegate->assess(10, 100);
    }

    server_delegate->assess(workers_count * clients_per_worker * connections_per_client, messages_per_connection);

}