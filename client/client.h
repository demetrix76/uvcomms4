#pragma once

#include <commlib/Streamer.h>
#include <thread>
#include <future>

namespace uvcomms4
{

class Client: public Streamer<Client>
{
public:
    friend UVPipeT<Client>;

    Client(config const & aConfig);
    ~Client();

private:
    void threadFunction(std::promise<void> aInitPromise);

private:
    std::thread mThread;
};


}
