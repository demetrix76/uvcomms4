#pragma once

#include "commlib.h"
#include <uv.h>
#include <atomic>

namespace uvcomms4
{

/** Manages multiple pipes, providing read/write functionality.

*/

template<typename impl_t>
class Streamer
{
public:
    Streamer(config const & aConfig);

protected:
    // called from the I/O thread
    int init(uv_loop_t * aLoop);

    // called from the I/O thread
    // the caller must guarantee that it will run the loop at least once after
    void deinit();

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
inline int Streamer<impl_t>::init(uv_loop_t *aLoop)
{
    return 0;
}

template <typename impl_t>
inline void Streamer<impl_t>::deinit()
{
}

template <typename impl_t>
inline void Streamer<impl_t>::onAsync(uv_async_t *aAsync)
{
}


}
