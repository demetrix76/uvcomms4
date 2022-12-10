#pragma once

#include "commlib.h"
#include <uv.h>
#include <system_error>
#include <iostream>
#include <thread>
#include <type_traits>

namespace uvcomms4::detail
{
    template<typename T, typename ...Ts>
    constexpr bool is_type_oneof = (std::is_same_v<std::decay_t<T>, Ts> || ...);


    template<typename T>
    concept isUVHandleType = is_type_oneof<T,
        uv_handle_t, uv_timer_t, uv_prepare_t, uv_check_t, uv_idle_t,
        uv_async_t, uv_poll_t, uv_signal_t,uv_process_t, uv_stream_t,
        uv_tcp_t, uv_pipe_t, uv_tty_t, uv_udp_t, uv_poll_t // no uv_fs_event_t in this version? it's not universal anyway because of char* path
    >;


    /* Every handle has a pointer to uv_loop_t; if we store the owner pointer in the loop,
       we can reuse the handle data member for something more useful
    */

    /* Assuming that every handle's .data points to something derived from BaseHandle,
       or null if it does not require deletion.
       The structure pointed to by .data contains the handle as its member.
    */

    struct BaseHandle
    {
        using deleter_t = void(*)(BaseHandle*);

        BaseHandle(deleter_t aDeleter) :
            deleter(aDeleter)
        {}

        template<typename T>
        inline static void deleterFunction(BaseHandle *aWraper)
        {
            delete static_cast<T*>(aWraper);
        }

        static void close_cb(uv_handle_t* aHandle)
        {
            auto self = static_cast<BaseHandle*>(aHandle->data);
            if(self)
                self->deleter(self);
        }

        deleter_t deleter;
    };


    template<typename owner_t>
    class Loop
    {
    public:
        Loop(owner_t *aOwner)
        {
            // libuv returns negated POSIX error code
            if(int r = uv_loop_init(&mLoop); r != 0)
                throw std::system_error(std::error_code(-r, std::system_category()), "Failed to initialize an uv_loop_t");

            mLoop.data = aOwner;
        }

        ~Loop()
        {
            uv_walk(&mLoop, [](uv_handle_t * aHandle, void*){
                uv_close(aHandle, &BaseHandle::close_cb);
            }, nullptr);


            int maxcount = 10;
            int r;
            while( (r = uv_run(&mLoop, UV_RUN_NOWAIT) && 0 < (--maxcount)) )
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                // technically, sleep() is unlikely necessary here. We might just need to
                // finish the cycle of closing/closed handles that want to deliver their last will first

            if(r)
                std::cerr << "WARNING: there are still active handles in the uv_loop\n";

            if(int r = uv_loop_close(&mLoop); r < 0)
                std::cerr << "ERROR closing the uv_loop: " <<
                    std::error_code(-r, std::system_category()).message() << std::endl <<
                    std::endl;

        }

        owner_t *owner() noexcept
        {
            return static_cast<owner_t*>(mLoop.data);
        }

        operator uv_loop_t * () noexcept
        {
            return &mLoop;
        }

    private:
        uv_loop_t   mLoop {};
    };

    template<typename owner_t>
    struct cb
    {
        static void async(uv_async_t * aAsync)
        {
            return static_cast<owner_t*>(aAsync->loop->data)->onAsync(aAsync);
        }

        static void connection(uv_stream_t* aServer, int aStatus)
        {
            return static_cast<owner_t*>(aServer->loop->data)->onConnection(aServer, aStatus);
        }

        static void read(uv_stream_t* aStream, ssize_t aNread, const uv_buf_t* aBuf)
        {
            return static_cast<owner_t*>(aStream->loop->data)->onRead(aStream, aNread, aBuf);
        }

        static void alloc(uv_handle_t* aHandle, size_t aSuggested_size, uv_buf_t* aBuf)
        {
            return static_cast<owner_t*>(aHandle->loop->data)->onAlloc(aHandle, aSuggested_size, aBuf);
        }

    };


    struct UVPipe: BaseHandle
    {
        UVPipe(Descriptor aDescriptor) :
            BaseHandle { BaseHandle::deleterFunction<UVPipe> },
            mDescriptor(aDescriptor)
        {}

        Descriptor descriptor() const { return mDescriptor; }

        static UVPipe* init(Descriptor aDescriptor, uv_loop_t *aLoop, bool aIpc = false)
        {
            UVPipe *npipe = new UVPipe(aDescriptor);
            if(int r = uv_pipe_init(aLoop, &npipe->mPipe, (int)aIpc); r < 0)
            {
                delete npipe;
                return nullptr;
            }
            npipe->mPipe.data = npipe;
            return npipe;
        }

        static UVPipe* fromHandle(uv_pipe_t * aHandle)
        {
            return static_cast<UVPipe*>(aHandle->data);
        }

        static UVPipe* fromHandle(uv_stream_t * aHandle)
        {
            return static_cast<UVPipe*>(aHandle->data);
        }

        int bind(char const *aName)
        {
            return uv_pipe_bind(*this, aName);
        }

        template<typename owner_t>
        int listen()
        {
            return uv_listen(*this, 128, &cb<owner_t>::connection);
        }

        template<typename owner_t>
        int read_start()
        {
            return uv_read_start(*this, &cb<owner_t>::alloc, &cb<owner_t>::read);
        }

        operator uv_pipe_t * () noexcept { return &mPipe; }
        operator uv_stream_t* () noexcept { return reinterpret_cast<uv_stream_t*>(&mPipe); }
        operator uv_handle_t* () noexcept { return reinterpret_cast<uv_handle_t*>(&mPipe); }

        void close()
        {
            uv_close(*this, &BaseHandle::close_cb);
        }

        uv_pipe_t mPipe {};
        Descriptor  mDescriptor;
    };



}