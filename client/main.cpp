#include <iostream>

#include <commlib/client.h>
#include <commlib/piper.h>

class SampleClientDelegate: public uvcomms4::ClientDelegate
{
public:

    void onStartup(uvcomms4::Client *aClient) override
    {
        // reminder: Constructor thread
        mClient = aClient;
        std::cout << "[SampleClient] Startup\n";

        // normally, the IO loop will only start after this method finishes,
        // so we shouldn't make any blocking calls that depend on the IO loop;
        // or, call unlockIO() first once we're done initializing
    }

    void onShutdown() override
    {
        // reminder: Destructor thread
        std::cout << "[SampleClient] Shutdown\n";
    }

    void onMessage(uvcomms4::Descriptor aDescriptor, uvcomms4::Collector & aCollector) override
    {
        // reminder: IO thread
        // we MUST extract the message here; otherwise, we'll have an infinite loop
        auto [status, message] = aCollector.getMessage<std::string>();
        if(status == uvcomms4::CollectorStatus::HasMessage)
            std::cout << "[SampleClient] MESSAGE: " << message << std::endl;
    }

    void onNewPipe(uvcomms4::Descriptor aDescriptor) override
    {
        // reminder: IO thread
        std::cout << "[SampleClient] New pipe: " << aDescriptor << std::endl;
    }

    void onPipeClosed(uvcomms4::Descriptor aDescriptor, int aErrorCode) override
    {
        // reminder: IO thread
        std::cout << "[SampleClient] Pipe closed: " << aDescriptor << "; error code " << aErrorCode << std::endl;
    }

private:
    uvcomms4::Client       *mClient { nullptr };
};

void run_echo_client();

class PiperClientDelegate : public uvcomms4::PiperDelegate
{
public:
    void Startup(uvcomms4::Piper *aPiper) override
    {
        mClient = aPiper;
    }

    void Shutdown() noexcept override
    {

    }

    void onNewConnection(uvcomms4::Descriptor aListener, uvcomms4::Descriptor aPipe) override
    {
        // will not happen here
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
    uvcomms4::Piper *mClient { nullptr };
};

int main(int, char*[])
{
    // run_echo_client();
    // return 0;

    using namespace std::literals;

    std::cout << "Hi, client here\n";

    uvcomms4::config const & cfg = uvcomms4::config::get_default();
    std::string pipename = uvcomms4::pipe_name(cfg);


    uvcomms4::Piper client(std::make_shared<PiperClientDelegate>());

    client.connect(pipename, [](auto const & result){
        auto [descriptor, code] = result;
        std::cout << "Connect result " << code << std::endl;
    });

    // auto [descriptor, status] = client.connect(pipename).get();

    // std::cout << "Connect result: " << status << std::endl;

    // uvcomms4::Client client(cfg, std::make_shared<SampleClientDelegate>());

    // client.connect(uvcomms4::pipe_name(cfg), [&](auto result)mutable {
    //     using namespace std::literals;
    //     auto [descriptor, status] = result;
    //     if(0 == status)
    //         client.send(descriptor, "Wilkommen Bienvenue Welcome"s, [](int){});
    // });

    // auto [descriptor, status] = client.connect(uvcomms4::pipe_name(cfg)).get();

    // if(0 == status)
    //     client.send(descriptor, "Wilkommen Bienvenue Welcome"s, [](auto){});

    std::this_thread::sleep_for(std::chrono::seconds(1));


    return 0;
}
