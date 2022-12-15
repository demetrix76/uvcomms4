#include <gtest/gtest.h>

#include "echotest.h"

using namespace uvcomms4;

using echotest::EchoServerDelegate;
using echotest::EchoClientDelegate;
using namespace std;

//N.B. Attaching to a process in linux requires disabling ptrace protection:
//"This is due to kernel hardening in Linux; you can disable this behavior by echo 0 > /proc/sys/kernel/yama/ptrace_scope or by modifying it in /etc/sysctl.d/10-ptrace.conf"
//https://stackoverflow.com/questions/19215177/how-to-solve-ptrace-operation-not-permitted-when-trying-to-attach-gdb-to-a-pro

/** The test itself is not perfect:
 *  - the clients tend to disconnect early (relying on the wrong counters)
 *  - close_count() was written without temporary connect() failures
 *  without ECONNREFUSED (i.e. under normal load) the test passes;
 *  in either case, only the test code fails. piper works OK.
*/

// Just some silly test to rest assured that TSAN is on guard
void tfun(int *aMod)
{
    *aMod = 10;
    std::this_thread::sleep_for(std::chrono::seconds(1));
}


/* Single-threaded test to check how the Client handles ECONNREFUSED should it happen */
TEST(EchoTest, EchoTest1)
{
    // TSAN alert
    /*int aVal = 100;
    std::thread t1([&]{ tfun(&aVal); });
    aVal = 64;
    t1.join();*/

    configure_signals();
    Config const &cfg = Config::get_default();
    ensure_socket_directory_exists(cfg);
    delete_socket_file(cfg);
    string pipename = pipe_name(cfg);

    size_t connections_per_client = 100;
    size_t messages_per_connection = 100;

    shared_ptr<EchoServerDelegate> server_delegate = make_shared<EchoServerDelegate>();

    {
        Piper server(server_delegate);
        auto [listener, errCode] = server.listen(pipename).get();
        ASSERT_EQ(errCode, 0);

        latch completionLatch { 1 };
        shared_ptr<EchoClientDelegate> client_delegate = make_shared<EchoClientDelegate>(completionLatch);
        {
            Piper client(client_delegate);
            client_delegate->spinUp(pipename, connections_per_client, messages_per_connection);
            completionLatch.wait();
        }
        client_delegate->assess();
    }
    server_delegate->assess(connections_per_client, messages_per_connection);
}

// meant to be run on a separate thread, returning a list of delegates for assession
std::list<std::shared_ptr<echotest::EchoClientDelegate>>
clientWorker(std::size_t aWorkerId, std::string aPipeName, std::size_t aClientCount,
    std::size_t aConnectionsPerClient, std::size_t aMessagesPerConnection)
{
    try
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
            std::cout << "Worker " << aWorkerId << " done waiting\n";
        }

        return delegates;
    }
    catch(std::exception &e)
    {
        std::cerr << "Exception in clientWorker: " << e.what() << std::endl;
        return {};
    }
}

TEST(EchoTest, EchoTest2)
{
    configure_signals();
    Config const &cfg = Config::get_default();
    ensure_socket_directory_exists(cfg);
    delete_socket_file(cfg);
    string pipename = pipe_name(cfg);

    size_t workers_count = 10;
    size_t clients_per_worker = 10;
    size_t connections_per_client = 10;
    size_t messages_per_connection = 1000;

    shared_ptr<EchoServerDelegate> server_delegate = make_shared<EchoServerDelegate>();

    {
        Piper server(server_delegate);
        auto [listener, errCode] = server.listen(pipename).get();
        ASSERT_EQ(errCode, 0);

        using worker_result = std::list<std::shared_ptr<echotest::EchoClientDelegate>>;

        std::list<std::future<worker_result>> wks;
        for(std::size_t i = 0; i < workers_count; i++)
        {
            wks.emplace_back(std::async(std::launch::async, [=]{
                return clientWorker(i, pipename, clients_per_worker,
                    connections_per_client, messages_per_connection);
            }));
        }

        std::list<worker_result> results;
        for(auto &wk: wks)
            results.emplace_back(wk.get());

        for(auto &result: results)
        {
            EXPECT_FALSE(result.empty());
            for(auto &delegate: result)
            {
                delegate->assess();
            }
        }

    }

    server_delegate->assess(workers_count * clients_per_worker * connections_per_client, messages_per_connection);

}