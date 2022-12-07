#pragma once

#include "commlib.h"
#include "uvx.h"
#include "uvwrite.h"
#include <uv.h>
#include <atomic>
#include <cstdlib>
#include <cassert>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <stdexcept>
#include <limits>

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

    template<MessageableContainer message_t>
    std::future<int> send(Descriptor aPipeDescriptor, message_t && aMessage);


    template<MessageableContainer message_t, MessageSendCallback callback_t>
    void send(Descriptor aPipeDescriptor, message_t && aMessage, callback_t && aCallback);


protected:
    // called on the I/O thread
    int streamer_init(uv_loop_t * aLoop);

    // called on the I/O thread
    // the caller must guarantee that it will run the loop at least once after
    void streamer_deinit();

    void request_stop();

    void trigger_async();

    Descriptor next_descriptor();

    void onAlloc(uv_handle_t* aHandle, size_t aSuggested_size, uv_buf_t* aBuf);
    void onRead(uv_stream_t* aStream, ssize_t aNread, const uv_buf_t* aBuf);
    void onWrite(uv_write_t* aReq, int aStatus);

    // take control of the pipe
    void adopt(UVPipe* aPipe);

private:
    void onAsyncBase(uv_async_t * aAsync);

    void pendingWritesProcess();


protected:
    config              mConfig;
private:
    uv_async_t          mAsyncTrigger {};
    bool                mAsyncInitialized { false };
    std::atomic<bool>   mStopRequested { false };
    Descriptor          mNextDescriptor { 1 }; // always accessed on the IO thread

    std::unordered_map<Descriptor, UVPipe*> mPipes; // always accessed on the IO thread

    std::mutex          mMx;
    std::vector<detail::IWriteRequest::pointer_t>   mWriteQueue;
    std::vector<detail::IWriteRequest::pointer_t>   mWriteQueueTemporary; // for quick swap, only accessed on the IO thread


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
            static_cast<Streamer<impl_t>*>(aAsync->data)->onAsyncBase(aAsync);
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
inline void Streamer<impl_t>::trigger_async()
{
    uv_async_send(&mAsyncTrigger);
}

template <typename impl_t>
inline Descriptor Streamer<impl_t>::next_descriptor()
{
    return mNextDescriptor++;
}

template <typename impl_t>
inline void Streamer<impl_t>::onAsyncBase(uv_async_t *aAsync)
{
    if(mStopRequested.load())
    {
        // todo pendingWritesAbort();
        uv_stop(aAsync->loop);
        return;
    }

    pendingWritesProcess();

    // call descendant's onAsync if it provides one
    if constexpr(requires (impl_t* impl, uv_async_t *a) { impl->onAsync(a); })
        static_cast<impl_t*>(this)->onAsync(aAsync);

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
    }
    std::free(aBuf->base);
}


template <typename impl_t>
inline void Streamer<impl_t>::adopt(UVPipe *aPipe) // only on IO thread
{
    auto found = mPipes.find(aPipe->descriptor());
    assert(found == mPipes.end());
    mPipes.insert({aPipe->descriptor(), aPipe});
}


template <typename impl_t>
template <MessageableContainer message_t>
inline std::future<int> Streamer<impl_t>::send(Descriptor aPipeDescriptor, message_t &&aMessage)
{
    if(std::size(aMessage) > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("IPC message length exceeds the limit");

    auto write_req = detail::nonarray_unique_ptr(new detail::WriteRequest(aPipeDescriptor, std::forward<message_t>(aMessage)));
    auto ret_future = write_req->get_future();
    {
        std::lock_guard lk(mMx);
        mWriteQueue.emplace_back(std::move(write_req));
    }
    uv_async_send(&mAsyncTrigger);

    return ret_future;
}


template <typename impl_t>
template <MessageableContainer message_t, MessageSendCallback callback_t>
inline void Streamer<impl_t>::send(Descriptor aPipeDescriptor, message_t &&aMessage, callback_t &&aCallback)
{
    auto write_req = detail::nonarray_unique_ptr(new detail::WriteRequest(
        aPipeDescriptor, std::forward<message_t>(aMessage),
        std::forward<callback_t>(aCallback)
    ));
    {
        std::lock_guard lk(mMx);
        mWriteQueue.emplace_back(std::move(write_req));
    }
    uv_async_send(&mAsyncTrigger);
}


template <typename impl_t>
inline void Streamer<impl_t>::pendingWritesProcess() // called on the IO thread
{
    mWriteQueueTemporary.clear();
    {
        std::lock_guard lk(mMx);
        mWriteQueueTemporary.swap(mWriteQueue);
    }

    for(std::unique_ptr<detail::IWriteRequest> & req: mWriteQueueTemporary)
    {
        auto found_pipe = mPipes.find(req->pipeDescriptor);
        if(found_pipe == mPipes.end())
        {
            req->complete(UV_ENOTCONN);
            req.reset();
        }
        else
        {
            UVPipe *thePipe = found_pipe->second;

            uv_buf_t bufs[] = {
                uv_buf_init(req->header, sizeof(req->header)),
                // because we're dealing with C...
                uv_buf_init(const_cast<char*>(req->data()), static_cast<unsigned>(req->size()))
            };

            int r = uv_write(&req->uv_write_request, *thePipe, bufs, std::size(bufs), &UVPipe::write_cb);

            if(r < 0)
            {
                // failed immediately, report back and destroy the request
                req->complete(r);
                req.reset();
            }
            else
            {
                // relinquish control to libuv, WriteRequest will report back and free the request in onWrite
                req.release();
            }
        }
    }

    mWriteQueueTemporary.clear();

}

template <typename impl_t>
inline void Streamer<impl_t>::onWrite(uv_write_t *aReq, int aStatus)
{
    std::unique_ptr<detail::IWriteRequest> iwrq { static_cast<detail::IWriteRequest*>(aReq->data) };
    iwrq->complete(aStatus);
}


}
