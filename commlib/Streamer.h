#pragma once

#include "commlib.h"
#include <uv.h>
#include <atomic>

namespace uvcomms4
{

/** Manages multiple pipes, providing read/write functionality.
 * Client and Server must be based on Streamer
*/

template<typename impl_t>
class Streamer
{
public:
    Streamer(config const & aConfig);

protected:
    // called on the I/O thread
    int streamer_init(uv_loop_t * aLoop);

    // called on the I/O thread
    // the caller must guarantee that it will run the loop at least once after
    void streamer_deinit();

    void request_stop();

private:
    void onAsync(uv_async_t * aAsync);

protected:
    config              mConfig;
    uv_async_t          mAsyncTrigger {};
    bool                mAsyncInitialized { false };
    std::atomic<bool>   mStopRequested { false };
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
}

template <typename impl_t>
inline void Streamer<impl_t>::request_stop()
{
    mStopRequested.store(true);
    uv_async_send(&mAsyncTrigger);
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


}
