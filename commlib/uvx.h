#pragma once

#include <uv.h>
#include <system_error>
#include <iostream>
#include <stdexcept>

namespace uvx
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
void uv_close(handle_t * aHandle, uv_close_cb aCloseCb = nullptr)
{
    ::uv_close(reinterpret_cast<uv_handle_t*>(aHandle), aCloseCb);
}

template<typename handle_t>
void uv_close(handle_t & aHandle, uv_close_cb aCloseCb = nullptr)
{
    ::uv_close(reinterpret_cast<uv_handle_t*>(&aHandle), aCloseCb);
}

}
