#include <iostream>

#include <commlib/piper.h>
#include <string>

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
    run_echo_client();
    return 0;

    using namespace std::literals;

    std::cout << "Hi, client here\n";

    uvcomms4::Config const & cfg = uvcomms4::Config::get_default();
    std::string pipename = uvcomms4::pipe_name(cfg);


    uvcomms4::Piper client(std::make_shared<PiperClientDelegate>());

    // client.connect(pipename, [](auto const & result){
    //     auto [descriptor, code] = result;
    //     std::cout << "Connect result " << code << std::endl;
    // });

    auto [descriptor, status] = client.connect(pipename).get();

    std::cout << "Connect result: " << status << std::endl;

    if(0 == status)
    {
        auto wrstatus = client.write(descriptor, "Some message to write"s).get();
        std::cout << "Write result " << wrstatus << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    return 0;
}
