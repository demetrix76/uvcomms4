#pragma once

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


    // probably not needed
    struct AsyncHandle : BaseHandle
    {
        AsyncHandle() :
            BaseHandle { BaseHandle::deleterFunction<AsyncHandle> }
            {}

        uv_async_t mAsync;
    };


    template<typename owner_t>
    struct cb
    {
        static void async(uv_async_t * aAsync)
        {
            return static_cast<owner_t*>(aAsync->loop->data)->onAsync(aAsync);
        }
    };

}