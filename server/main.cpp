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

    void onNewConnection(uvcomms4::Descriptor aListener, uvcomms4::Descriptor aPipe) override
    {
        std::cout << "Accepted new connection on listener " << aListener <<
            "; new pipe is " << aPipe << std::endl;
    }

    void onPipeClosed(uvcomms4::Descriptor aPipe, int aErrCode) override
    {
        std::cout << "Pipe " << aPipe << " closed; error code " << aErrCode << std::endl;
    }

    void onMessage(uvcomms4::Descriptor aDescriptor, uvcomms4::Collector & aCollector)
    {
        auto [status, message] = aCollector.getMessage<std::string>();
        if(status == uvcomms4::CollectorStatus::HasMessage)
            std::cout << "MESSAGE: " << message << std::endl;
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

    uvcomms4::config const & cfg = uvcomms4::config::get_default();
    uvcomms4::ensure_socket_directory_exists(cfg);
    uvcomms4::delete_socket_file(cfg);

    std::string pipename = uvcomms4::pipe_name(cfg);

    try
    {
        //uvcomms4::Server server(uvcomms4::config::get_default(), std::make_shared<SampleServerDelegate>());
        uvcomms4::Piper server(std::make_shared<PiperServerDelegate>());

        // server.listen("/aaa/nnn", [](uvcomms4::Descriptor desc){
        //     std::cout << "Listen result " << desc << std::endl;
        // });

        auto [desc, errcode] = server.listen(pipename.c_str()).get();
        std::cout << "Listen result " << errcode << std::endl;
        //server.listen(pipename.c_str(), [](std::tuple<uvcomms4::Descriptor, int>){});

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
