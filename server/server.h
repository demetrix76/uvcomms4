#pragma once

#include <commlib/commlib.h>
#include <commlib/uvx.h>
#include <commlib/Streamer.h>
#include <uv.h>
#include <thread>
#include <future>
#include <atomic>

namespace uvcomms4
{

class Server : public Streamer<Server>
{
public:
    friend UVPipeT<Server>;

    Server(config const & aConfig);
    ~Server();

private:
    void threadFunction(std::promise<void> aInitPromise);

    void onConnection(uv_stream_t* aServer, int aStatus);

private:
    std::thread mThread;
    uv_pipe_t   mListeningPipe {};
};


}
