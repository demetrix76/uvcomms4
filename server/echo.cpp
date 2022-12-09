
#include <commlib/server.h>
#include <commlib/commlib.h>
#include <iostream>

using namespace uvcomms4;


class EchoServerDelegate: public ServerDelegate
{
public:

    void onStartup(Server *aServer) override
    {
        // reminder: Constructor thread
        mServer = aServer;
        std::cout << "[EchoServer] Startup\n";
    }

    void onShutdown() override
    {
        // reminder: Destructor thread
        std::cout << "[EchoServer] Shutdown\n";
    }

    void onMessage(Descriptor aDescriptor, Collector & aCollector) override
    {
        // reminder: IO thread
        // we MUST extract the message here; otherwise, we'll have an infinite loop
        auto [status, message] = aCollector.getMessage<std::string>();
        if(status == CollectorStatus::HasMessage)
        {
            mServer->send(aDescriptor, std::move(message), [](auto){});
        }
    }

    void onNewPipe(Descriptor aDescriptor) override
    {
        // reminder: IO thread
    }

    void onPipeClosed(Descriptor aDescriptor, int aErrorCode) override
    {
        // reminder: IO thread
        if(aErrorCode)
            std::cerr << "Pipe error: " << aErrorCode << std::endl;
    }

private:
    Server       *mServer { nullptr };
};



void echo_run()
{
    std::cout << "Running echo server...\n";

    Server server(config::get_default(), std::make_shared<EchoServerDelegate>());

    std::cout << "Hit Enter to stop\n";
    std::string s;
    std::getline(std::cin, s, '\n');

}