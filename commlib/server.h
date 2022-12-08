#pragma once

#include "commlib.h"
#include "uvx.h"
#include "Streamer.h"
#include <uv.h>
#include <thread>
#include <future>
#include <atomic>

namespace uvcomms4
{

class Server final : public Streamer<Server>
{
public:
    friend UVPipeT<Server>;

    Server(config const & aConfig, ServerDelegate::pointer aDelegate);
    ~Server();

private:
    void threadFunction(std::promise<void> aInitPromise);

    void onConnection(uv_stream_t* aServer, int aStatus);

private:
    std::thread mThread;
    uv_pipe_t   mListeningPipe {};
};


}
