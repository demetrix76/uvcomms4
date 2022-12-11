
#include <commlib/client.h>
#include <commlib/piper.h>
#include <commlib/commlib.h>

#include <iostream>
#include <string>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <future>
#include <list>
#include <algorithm>
#include <iterator>

using namespace uvcomms4;

class EchoClientDelegate: public ClientDelegate
{
public:
    static constexpr int repeat_count = 100;

    EchoClientDelegate(std::promise<void> && aCompletionPromise) :
        mCompletionPromise(std::move(aCompletionPromise))
    {}

    void onStartup(Client *aClient) override
    {
        // reminder: Constructor thread
        mClient = aClient;

        aClient->connect(pipe_name(config::get_default()),[this](auto result){
            using namespace std::literals;
            auto [descriptor, status] = result;
            if(0 == status)
            {
                sendRandomMessage(descriptor, repeat_count);
            }
            else
            {
                throw std::runtime_error("Failed to connect");
            }
        });
    }

    void onShutdown() override
    {
        // reminder: Destructor thread
    }

    void onMessage(Descriptor aDescriptor, Collector & aCollector) override
    {
        // reminder: IO thread
        // we MUST extract the message here; otherwise, we'll have an infinite loop
        auto [status, message] = aCollector.getMessage<std::string>();
        if(status == CollectorStatus::HasMessage)
        {
            if(!checkExpectedMessage(message))
                throw std::runtime_error("Messages do not match");
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

    // =========================

    void addExpectedMessage(std::string const &aMessage)
    {
        std::lock_guard lk(mMx);
        mExpectedMessages.push_back(aMessage);
    }

    bool checkExpectedMessage(std::string const &aMessage)
    {
        std::string front_msg;
        std::lock_guard lk(mMx);
        {
            if(mExpectedMessages.empty())
                return false;
            front_msg = std::move(mExpectedMessages.front());
            mExpectedMessages.pop_front();
        }
        if(0 == --mCounter)
            mCompletionPromise.set_value();
        return front_msg == aMessage;
    }

    void sendRandomMessage(Descriptor aDescriptor, int aRemaining)
    {
        std::cout << aRemaining << std::endl;
        if(aRemaining > 0)
        {
            std::string s = "Some Message";
            addExpectedMessage(s);
            mClient->send(aDescriptor, std::move(s), [=, this](int aErrCode){
                if(aErrCode)
                    throw std::system_error(std::error_code(-aErrCode, std::system_category()), "SEND");
                else
                    sendRandomMessage(aDescriptor, aRemaining - 1);
            });
        }
    }

private:
    Client       *mClient { nullptr };
    int           mCounter { repeat_count };

    std::mutex              mMx;
    std::deque<std::string> mExpectedMessages;

    std::promise<void>      mCompletionPromise;
};


class EchoClientDelegate2: public PiperDelegate
{
public:
    static constexpr int repeat_count = 100;

    EchoClientDelegate2(std::promise<void> && aCompletionPromise) :
        mCompletionPromise(std::move(aCompletionPromise))
    {}

    void Startup(Piper * aPiper) override
    {
        mClient = aPiper;
        std::string pname = pipe_name(config::get_default());
        aPiper->connect(pname, [this](auto result){
            auto [descriptor, status] = result;
            if(0 == status)
            {
                sendRandomMessage(descriptor, repeat_count);
            }
            else
            {
                throw std::runtime_error("Failed to connect");
            }
        });
    }

    void Shutdown() noexcept override
    {

    }

    void onNewConnection(Descriptor aListener, Descriptor aPipe) override
    {

    }

    void onPipeClosed(Descriptor aPipe, int aErrCode) override
    {

    }

    void onMessage(Descriptor aDescriptor, Collector & aCollector) override
    {
        auto [status, message] = aCollector.getMessage<std::string>();
        if(status == CollectorStatus::HasMessage)
        {
            if(!checkExpectedMessage(message))
                throw std::runtime_error("Messages do not match");
        }
    }

    // ===============
    void addExpectedMessage(std::string const &aMessage)
    {
        std::lock_guard lk(mMx);
        mExpectedMessages.push_back(aMessage);
    }

    bool checkExpectedMessage(std::string const &aMessage)
    {
        std::string front_msg;
        std::lock_guard lk(mMx);
        {
            if(mExpectedMessages.empty())
                return false;
            front_msg = std::move(mExpectedMessages.front());
            mExpectedMessages.pop_front();
        }
        if(0 == --mCounter)
            mCompletionPromise.set_value();
        return front_msg == aMessage;
    }

    void sendRandomMessage(Descriptor aDescriptor, int aRemaining)
    {
        std::cout << aRemaining << std::endl;
        if(aRemaining > 0)
        {
            std::string s = "Some Message"; // TODO make it actually random
            addExpectedMessage(s);
            mClient->write(aDescriptor, std::move(s), [=, this](int aErrCode){
                if(0 != aErrCode)
                    throw std::system_error(std::error_code(-aErrCode, std::system_category()), "SEND");
                else
                    sendRandomMessage(aDescriptor, aRemaining - 1);
            });
        }
    }

private:
    Piper *mClient { nullptr };

    int              mCounter { repeat_count };

    std::mutex              mMx;
    std::deque<std::string> mExpectedMessages;

    std::promise<void>      mCompletionPromise;
};

void run_echo_client()
{
    // std::promise<void> completionPromise;
    // auto completionFuture = completionPromise.get_future();
    // Client client(config::get_default(), std::make_shared<EchoClientDelegate>(std::move(completionPromise)));

    // completionFuture.get();
    //config const & cfg = config::get_default();

    constexpr std::size_t client_count = 100;

    std::list<std::promise<void>> completionPromises;
    completionPromises.resize(client_count);

    std::list<std::future<void>> completionFutures;
    std::transform(completionPromises.begin(), completionPromises.end(), std::back_inserter(completionFutures),
        [](std::promise<void> & pms){
            return pms.get_future();
        });

    std::list<std::unique_ptr<Piper>> clients;
    std::transform(completionPromises.begin(), completionPromises.end(), std::back_insert_iterator(clients),
        [&](std::promise<void> & pms){
            return std::make_unique<Piper>(std::make_shared<EchoClientDelegate2>(std::move(pms)));
        });

    for(auto & f: completionFutures)
        f.wait();

    //std::this_thread::sleep_for(std::chrono::seconds(1));

}