#pragma once

#include <commlib/commlib.h>
#include <uv.h>
#include <thread>
#include <future>
#include <atomic>

namespace uvcomms4
{

class Server
{
public:
    Server(config const & aConfig);
    ~Server();

private:
    void threadFunction(std::promise<void> aInitPromise);

    void onAsync(uv_async_t *aAsync);
    void onConnection(uv_stream_t* aServer, int aStatus);

private:
    config      mConfig {};
    std::thread mThread;
    uv_async_t  mAsyncTrigger {};
    uv_pipe_t   mListeningPipe {};

    std::atomic<bool> mStopRequested { false };
};


}
