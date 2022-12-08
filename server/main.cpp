#include <iostream>

#include <commlib/server.h>
#include <commlib/delegate.h>
#include <vector>
#include <memory>

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>


class SampleServerDelegate: public uvcomms4::ServerDelegate
{
public:

    void onStartup(uvcomms4::Server *aServer) override
    {
        // reminder: Constructor thread
        mServer = aServer;
        std::cout << "[SampleServer] Startup\n";
    }

    void onShutdown() override
    {
        // reminder: Destructor thread
        std::cout << "[SampleServer] Shutdown\n";
    }

    void onMessage(uvcomms4::Descriptor aDescriptor, uvcomms4::Collector & aCollector) override
    {
        // reminder: IO thread
        // we MUST extract the message here; otherwise, we'll have an infinite loop
        auto [status, message] = aCollector.getMessage<std::string>();
        if(status == uvcomms4::CollectorStatus::HasMessage)
            std::cout << "[SampleServer] MESSAGE: " << message << std::endl;
    }

    void onNewPipe(uvcomms4::Descriptor aDescriptor) override
    {
        // reminder: IO thread
        std::cout << "[SampleServer] New pipe: " << aDescriptor << std::endl;
    }

    void onPipeClosed(uvcomms4::Descriptor aDescriptor, int aErrorCode) override
    {
        // reminder: IO thread
        std::cout << "[SampleServer] Pipe closed: " << aDescriptor << "; error code " << aErrorCode << std::endl;
    }

private:
    uvcomms4::Server       *mServer { nullptr };
};

int main(int, char*[])
{
    using namespace std::literals;
    std::cout << "Hi there\n";
    try
    {
        uvcomms4::Server server(uvcomms4::config::get_default(), std::make_shared<SampleServerDelegate>());

        std::cout << "Hit Enter to stop...\n";
        std::string s;
        std::getline(std::cin, s, '\n');
    }
    catch(std::exception &e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
