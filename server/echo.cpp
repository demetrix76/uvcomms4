
#include <commlib/piper.h>
#include <commlib/commlib.h>
#include <iostream>

using namespace uvcomms4;

class EchoServerDelegate: public PiperDelegate
{
public:
        void Startup(Piper * aPiper) override
        {
            mServer = aPiper;
            std::cout << "[EchoServer] Startup\n";
        }

        void Shutdown() noexcept override
        {
            std::cout << "[EchoServer] Shutdown\n";
        }

        void onNewConnection(Descriptor aListener, Descriptor aPipe) noexcept override
        {

        }

        void onPipeClosed(Descriptor aPipe, int aErrCode) noexcept override
        {
            if(aErrCode)
                std::cerr << "Pipe error: " << aErrCode << std::endl;
        }

        void onMessage(Descriptor aDescriptor, Collector & aCollector) noexcept override
        {
            // reminder: IO thread
            // we MUST extract the message here; otherwise, we'll have an infinite loop
            auto [status, message] = aCollector.getMessage<std::string>();
            if(status == CollectorStatus::HasMessage)
            {
                mServer->write(aDescriptor, std::move(message), [](auto){});
            }
        }

private:
    Piper   *mServer { nullptr };
};


void echo_run()
{
    std::cout << "Running echo server...\n";

    Config const &cfg = Config::get_default();
    ensure_socket_directory_exists(cfg);
    delete_socket_file(cfg);

    Piper server(std::make_shared<EchoServerDelegate>());

    auto [listener, errCode] = server.listen(pipe_name(cfg)).get();

    std::cout << "Listen result " << errCode << std::endl;

    std::cout << "Hit Enter to stop\n";
    std::string s;
    std::getline(std::cin, s, '\n');

}