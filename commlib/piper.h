#pragma once

#include "delegate.h"
#include "wrappers.h"
#include <uv.h>
#include <memory>
#include <future>
#include <thread>
#include <mutex>

namespace uvcomms4
{

class Piper
{
public:
    friend struct detail::cb<Piper>;

    Piper(PiperDelegate::pointer aDelegate);
    ~Piper();

    Piper(Piper const &) = delete;
    Piper & operator = (Piper const &) = delete;

private:
    void threadFunction(std::promise<void> aInitPromise);

    void requestStop();

    void onAsync(uv_async_t *aAsync);

    void requireIOThread();
    void requireNonIOThread();

private:
    PiperDelegate::pointer  mDelegate { nullptr };
    std::thread             mIOThread;
    std::thread::id         mIOThreadId {};

    bool                    mStopFlag { false };
    uv_async_t              mAsyncTrigger {};
    std::mutex              mMx;

};


}
