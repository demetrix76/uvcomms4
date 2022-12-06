#pragma once

#include "commlib.h"
#include "uvx.h"
#include <uv.h>
#include <atomic>
#include <cstdlib>
#include <cassert>
#include <unordered_map>

namespace uvcomms4
{

/** Manages multiple pipes, providing read/write functionality.
 * Client and Server must be based on Streamer
*/


template<typename impl_t>
class Streamer
{
public:
    using UVPipe = UVPipeT<impl_t>;

    Streamer(config const & aConfig);

protected:
    // called on the I/O thread
    int streamer_init(uv_loop_t * aLoop);

    // called on the I/O thread
    // the caller must guarantee that it will run the loop at least once after
    void streamer_deinit();

    void request_stop();

    Descriptor next_descriptor();

    void onAlloc(uv_handle_t* aHandle, size_t aSuggested_size, uv_buf_t* aBuf);
    void onRead(uv_stream_t* aStream, ssize_t aNread, const uv_buf_t* aBuf);
    void onWrite(uv_write_t* aReq, int aStatus);

    void adopt(UVPipe* aPipe);

private:
    void onAsync(uv_async_t * aAsync);


protected:
    config              mConfig;
private:
    uv_async_t          mAsyncTrigger {};
    bool                mAsyncInitialized { false };
    std::atomic<bool>   mStopRequested { false };
    Descriptor          mNextDescriptor { 1 }; // always accessed on the IO thread

    std::unordered_map<Descriptor, UVPipe*> mPipes;

};

//=================================================================================
// IMPLEMENTATION
//=================================================================================

template <typename impl_t>
inline Streamer<impl_t>::Streamer(config const &aConfig) :
    mConfig(aConfig)
{}

template <typename impl_t>
inline int Streamer<impl_t>::streamer_init(uv_loop_t *aLoop)
{
    int r = uv_async_init(aLoop, &mAsyncTrigger,
        [](uv_async_t *aAsync){
            static_cast<Streamer<impl_t>*>(aAsync->data)->onAsync(aAsync);
        });
    mAsyncTrigger.data = this;
    if(r < 0)
        return r;
    mAsyncInitialized = true;
    return 0;
}

template <typename impl_t>
inline void Streamer<impl_t>::streamer_deinit()
{
    if(mAsyncInitialized)
        uv_close(reinterpret_cast<uv_handle_t*>(&mAsyncTrigger), nullptr);

    for(auto [k, p] : mPipes)
        if(!uv_is_closing(*p))
            p->close();
}

template <typename impl_t>
inline void Streamer<impl_t>::request_stop()
{
    mStopRequested.store(true);
    uv_async_send(&mAsyncTrigger);
}

template <typename impl_t>
inline Descriptor Streamer<impl_t>::next_descriptor()
{
    return mNextDescriptor++;
}

template <typename impl_t>
inline void Streamer<impl_t>::onAsync(uv_async_t *aAsync)
{
    if(mStopRequested.load())
    {
        uv_stop(aAsync->loop);
    }
    // todo check for write commands
    // todo do we need to also call the descendant?
}


template <typename impl_t>
inline void Streamer<impl_t>::onAlloc(uv_handle_t *aHandle, size_t aSuggested_size, uv_buf_t *aBuf)
{
    // memory allocated must be freed in onRead
    UVPipe* thePipe = UVPipe::fromHandle(aHandle);
    size_t sz = thePipe->recvBufferSize();
    if(0 == sz) sz = aSuggested_size;
    aBuf->base = static_cast<char*>(std::malloc(sz));
    aBuf->len = aBuf->base ? sz : 0;
}


template <typename impl_t>
inline void Streamer<impl_t>::onRead(uv_stream_t *aStream, ssize_t aNread, const uv_buf_t *aBuf)
{
    if(aNread == UV_EOF)
    {
        std::cout << "EOF reached\n";
        // we need to close the pipe but somehow prevent crashes if more write requests arrive
        // also need to check if we have a complete message received
    }
    else if(aNread < 0)
    {
        report_uv_error(std::cerr, (int)aNread, "Error reading from a pipe");
        // we need to close the pipe but somehow prevent crashes if more write requests arrive
    }
    else
    {
        // N.B. zero length reads are possible, avoid adding such buffers
        std::cout << "Received " << aNread << " bytes\n";
        std::free(aBuf->base);
    }
}


template <typename impl_t>
inline void Streamer<impl_t>::onWrite(uv_write_t *aReq, int aStatus)
{
}

template <typename impl_t>
inline void Streamer<impl_t>::adopt(UVPipe *aPipe) // only on IO thread
{
    auto found = mPipes.find(aPipe->descriptor());
    assert(found == mPipes.end());
    mPipes.insert({aPipe->descriptor(), aPipe});
}


}
