#pragma once

#include "delegate.h"
#include "wrappers.h"
#include "request.h"
#include <uv.h>
#include <memory>
#include <future>
#include <thread>
#include <mutex>
#include <vector>
#include <concepts>

namespace uvcomms4
{

class Piper :
    requests::RequestHandler
{
public:
    friend struct detail::cb<Piper>;

    Piper(PiperDelegate::pointer aDelegate);
    ~Piper();

    Piper(Piper const &) = delete;
    Piper & operator = (Piper const &) = delete;

    std::future<std::tuple<Descriptor, int>> listen(std::string const & aListenAddress);

    template<std::invocable<Descriptor> callback_t>
    void listen(std::string const & aListenAddress, callback_t && aCallback);

private:
    void threadFunction(std::promise<void> aInitPromise);

    void requestStop();

    void onAsync(uv_async_t *aAsync);

    void onConnection(uv_stream_t* aServer, int aStatus); // incoming connection

    void onRead(uv_stream_t* aStream, ssize_t aNread, const uv_buf_t* aBuf);
    void onAlloc(uv_handle_t* aHandle, size_t aSuggested_size, uv_buf_t* aBuf);

    void requireIOThread();
    void requireNonIOThread();

    Descriptor nextDescriptor();

    void triggerAsync();

    void postRequest(requests::Request::pointer);

    void processPendingRequests(bool aAbort);

    void handleListenRequest(requests::ListenRequest *) override;

private:
    PiperDelegate::pointer  mDelegate { nullptr };
    std::thread             mIOThread;
    std::thread::id         mIOThreadId {};

    bool                    mStopFlag { false };
    uv_async_t              mAsyncTrigger {};
    std::mutex              mMx;

    Descriptor              mNextDescriptor { 1 }; // IO thread only

    std::vector<requests::Request::pointer>  mPendingRequests;
    std::vector<requests::Request::pointer>  mPendingRequestsTemporary; // quickly switch these two under lock

    uv_loop_t               *mRunningLoop { nullptr }; // only accessed on the IO thread

};

//============================================================================================
// IMPLEMENTATION
//============================================================================================

/** Creates a new socket/pipe, binds it to the specified address and starts listening
 *  for incoming connection on it.
 *  Returns (via future<>) the descriptor of the listening pipe.
 *  It is highly discouraged to call this variant from the IO thread (will deadlock)
*/
inline std::future<std::tuple<Descriptor, int>> Piper::listen(std::string const & aListenAddress)
{
    requireNonIOThread();

    std::promise<std::tuple<Descriptor, int>> thePromise;
    auto ret_future = thePromise.get_future();

    postRequest(
        requests::makeListenRequest(aListenAddress, requests::promisingCallback(std::move(thePromise)))
    );

    return ret_future;
}

/** Creates a new socket/pipe, binds it to the specified address and starts listening
 *  for incoming connection on it.
 *  'Returns' (via the supplied callback) the descriptor of the listening pipe.
 *  This callback will be called on the IO thread
*/
template <std::invocable<Descriptor> callback_t>
inline void Piper::listen(std::string const &aListenAddress, callback_t &&aCallback)
{
    postRequest(
        requests::makeListenRequest(aListenAddress, std::forward<callback_t>(aCallback))
    );
}

}
