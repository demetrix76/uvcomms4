#include <iostream>

#include <commlib/server.h>
#include <commlib/delegate.h>
#include <commlib/piper.h>
#include <vector>
#include <memory>

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

#include <type_traits>


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

void echo_run();

class PiperServerDelegate : public uvcomms4::PiperDelegate
{
public:
    void Startup(uvcomms4::Piper *aPiper) override
    {
        mServer = aPiper;
    }

    void Shutdown() noexcept override
    {

    }

private:
    uvcomms4::Piper *mServer { nullptr };
};


int main(int, char*[])
{

    signal(SIGPIPE, SIG_IGN);
    //echo_run();
    // return 0;

    using namespace std::literals;
    std::cout << "Hi there\n";
    try
    {
        //uvcomms4::Server server(uvcomms4::config::get_default(), std::make_shared<SampleServerDelegate>());
        uvcomms4::Piper server(std::make_shared<PiperServerDelegate>());

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
