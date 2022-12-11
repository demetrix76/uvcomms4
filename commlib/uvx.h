#pragma once

#include "commlib.h"
#include "collector.h"
#include <uv.h>
#include <system_error>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <concepts>
#include <ostream>
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace uvcomms4
{

/** A trivial RAII wrapper around uv_loop_t.
Our use scenario is that we create UVLoop on the stack in the thread function;
thus, uv_loop_close will be called at thread exit.
Also, two-phase initialization better suits our use scenario.
*/
class UVLoop
{
public:
    UVLoop()
    {
    }

    // throws std::system_error if uv_loop_init fails
    void init()
    {
        // libuv returns negated POSIX error code
        if(int r = uv_loop_init(&mLoop); r != 0)
            throw std::system_error(std::error_code(-r, std::system_category()), "Failed to create an UVLoop");
        mInitialized = true;
    }

    ~UVLoop()
    {
        if(mInitialized)
        {
            if(int r = uv_loop_close(&mLoop); r < 0)
            {
                std::cerr << "WARNING: error while closing an UVLoop: "
                    << std::error_code(-r, std::system_category()).message() << std::endl;
            }
        }
    }

    bool initialized() const noexcept
    {
        return mInitialized;
    }

    operator uv_loop_t* ()
    {
        if(!mInitialized)
            throw std::logic_error("UVLoop has not been initialized");
        return &mLoop;
    }

    uv_loop_t * operator -> ()
    {
        if(!mInitialized)
            throw std::logic_error("UVLoop has not been initialized");
        return &mLoop;
    }

private:
    uv_loop_t mLoop {};
    bool      mInitialized { false };
};


template<typename handle_t>
inline void uv_close_x(handle_t * aHandle, uv_close_cb aCloseCb = nullptr)
{
    ::uv_close(reinterpret_cast<uv_handle_t*>(aHandle), aCloseCb);
}

template<typename handle_t>
inline void uv_close_x(handle_t & aHandle, uv_close_cb aCloseCb = nullptr)
{
    ::uv_close(reinterpret_cast<uv_handle_t*>(&aHandle), aCloseCb);
}

/// aError is expected to be negative as per libuv convention
inline std::ostream & report_uv_error(std::ostream & aStream, int aError, std::string_view aMessage)
{
    return aStream << aMessage << ": "
        << std::error_code(-aError, std::system_category()).message()
        << std::endl;
}

/** Wrapper for dynamically allocated pipe handles.
uv_pipe_t::data points to this object;
this object points to the owner (Server/Client)
*/
template<typename owner_t>
class UVPipeT
{
public:
    UVPipeT(owner_t * aOwner, Descriptor aDescriptor):
        mOwner(aOwner), mDescriptor(aDescriptor)
    {
        // UV docs state libuv will not write the data member
        mPipe.data = this;
    }

    int init(uv_loop_t *aLoop, int aIpc)
    {
        int r = uv_pipe_init(aLoop, *this, aIpc);
        mPipe.data = this;
        return r;
    }

    void close(int aCloseCode = 0)
    {
        mCloseCode = aCloseCode;
        uv_close(*this, &close_cb);
    }

    int read_start()
    {
        int r = uv_read_start(*this, &alloc_cb, &read_cb);
#if defined(__APPLE__) || defined(__unix__)
        int bfsize = 0;
        uv_recv_buffer_size(reinterpret_cast<uv_handle_t*>(&mPipe), &bfsize);
#if defined(__linux__)
// as per libUV docs, "Linux will set double the size and return double the size of the original set value."
// apparently, this only means that the real buffer size is twice as big as requested; no need to divide it here
#endif
// we probably don't need big buffers as our messages will not be huge
        bfsize = std::min(std::abs(bfsize), 64 * 1024);
        mRecvBufferSize = bfsize;
#endif
        return r;
    }

    Descriptor descriptor() const noexcept
    {
        return mDescriptor;
    }

    Collector & collector()
    {
        return mCollector;
    }

    std::size_t recvBufferSize() const noexcept
    {
        return mRecvBufferSize;
    }

    owner_t *owner() noexcept
    {
        return mOwner;
    }

    uv_pipe_t* get() noexcept
    {
        return &mPipe;
    }

    operator uv_pipe_t* () noexcept
    {
        return &mPipe;
    }

    operator uv_stream_t* () noexcept
    {
        return reinterpret_cast<uv_stream_t*>(&mPipe);
    }

    operator uv_handle_t* () noexcept
    {
        return reinterpret_cast<uv_handle_t*>(&mPipe);
    }

    template<typename handle_t>
        requires requires (handle_t * h) {
            {h->data} -> std::convertible_to<void*>;
        }
    static UVPipeT* fromHandle(handle_t *aHandle)
    {
        return static_cast<UVPipeT*>(aHandle->data);
    }

    static void close_cb(uv_handle_t * aHandle)
    {
        static_assert(std::is_same_v<decltype(&UVPipeT<owner_t>::close_cb), ::uv_close_cb>);
        UVPipeT* p = UVPipeT::fromHandle(aHandle);
        p->owner()->onClose(p->descriptor(), p->mCloseCode);
        delete p;
    }

    static void alloc_cb(uv_handle_t* aHandle, size_t aSuggested_size, uv_buf_t* aBuf)
    {
        UVPipeT::fromHandle(aHandle)->owner()->onAlloc(aHandle, aSuggested_size, aBuf);
    }

    static void read_cb(uv_stream_t* aStream, ssize_t aNread, const uv_buf_t* aBuf)
    {
        UVPipeT::fromHandle(aStream)->owner()->onRead(aStream, aNread, aBuf);
    }

    static void write_cb(uv_write_t* aReq, int aStatus)
    {
        UVPipeT::fromHandle(aReq->handle)->owner()->onWrite(aReq, aStatus);
    }

    static void connect_cb(uv_connect_t* aReq, int aStatus)
    {
        UVPipeT::fromHandle(aReq->handle)->owner()->onConnect(aReq, aStatus);
    }

private:
    uv_pipe_t       mPipe {};
    owner_t*        mOwner { nullptr };
    int             mRecvBufferSize {0};
    std::uint64_t   mDescriptor {0};
    Collector       mCollector;
    int             mCloseCode; // error code to distinguish graceful close from abnormal disconnection
};





} //namespace uvcomms4
