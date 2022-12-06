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

class Server
{
public:
    friend uvx::UVPipeT<Server>;

    Server(config const & aConfig);
    ~Server();

private:
    void threadFunction(std::promise<void> aInitPromise);

    void onAsync(uv_async_t *aAsync);
    void onConnection(uv_stream_t* aServer, int aStatus);
    void onAlloc(uv_handle_t* aHandle, size_t aSuggested_size, uv_buf_t* aBuf);
    void onRead(uv_stream_t* aStream, ssize_t aNread, const uv_buf_t* aBuf);
    void onWrite(uv_write_t* aReq, int aStatus);

private:
    config      mConfig {};
    std::thread mThread;
    uv_async_t  mAsyncTrigger {};
    uv_pipe_t   mListeningPipe {};

    std::atomic<bool> mStopRequested { false };
};


}
