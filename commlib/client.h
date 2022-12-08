#pragma once

#include "Streamer.h"
#include <thread>
#include <future>
#include <memory>

namespace uvcomms4
{

struct ClientSharedData
{

};

class Client: public Streamer<Client>
{
public:
    friend UVPipeT<Client>;
    friend Streamer<Client>;

    Client(config const & aConfig, ClientDelegate::pointer aDelegate);
    ~Client();

    virtual void Connected(Descriptor aDescriptor);

private:
    void threadFunction(std::promise<void> aInitPromise);

    void onAsync(uv_async_t * aAsync);

    void tryConnect(uv_async_t * aAsync);

    void onConnect(uv_connect_t* aReq, int aStatus);

private:
    std::thread mThread;

    std::atomic<bool>   mShouldConnect { true };
};


}
