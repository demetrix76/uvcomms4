#include <iostream>

#include <commlib/piper.h>
#include <vector>
#include <memory>

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

#include <type_traits>


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
    echo_run();
    return 0;

    using namespace std::literals;
    std::cout << "Hi there\n";

    uvcomms4::config const & cfg = uvcomms4::config::get_default();
    uvcomms4::ensure_socket_directory_exists(cfg);
    uvcomms4::delete_socket_file(cfg);

    std::string pipename = uvcomms4::pipe_name(cfg);

    try
    {
        uvcomms4::Piper server(std::make_shared<PiperServerDelegate>());


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
