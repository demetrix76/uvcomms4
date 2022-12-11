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
#include <unordered_map>

namespace uvcomms4
{

class Piper :
    requests::RequestHandler
{
public:
    using UVPipe = detail::UVPipeT<Piper>;
    friend UVPipe;
    friend struct detail::cb<Piper>;

    Piper(PiperDelegate::pointer aDelegate);
    ~Piper();

    Piper(Piper const &) = delete;
    Piper & operator = (Piper const &) = delete;

    /** Creates a new socket/pipe, binds it to the specified address and starts listening
     *  for incoming connection on it.
     *  Returns (via future<>) the descriptor + error code of the listening pipe.
     *  It is highly discouraged to call this variant from the IO thread (will deadlock)
    */
    std::future<std::tuple<Descriptor, int>> listen(std::string const &aListenAddress);

    /** Creates a new socket/pipe, binds it to the specified address and starts listening
     *  for incoming connection on it.
     *  'Returns' (via the supplied callback) the descriptor + error code of the listening pipe.
     *  This callback will be called on the IO thread
    */
    template<std::invocable<std::tuple<Descriptor, int>> callback_t>
    void listen(std::string const & aListenAddress, callback_t && aCallback);

    /** Creates a new socket/pipe and attempts to connect to a remote address.
     *  Returns (via future<>) the descriptor + error code of the listening pipe.
     *  It is highly discouraged to call this variant from the IO thread (will deadlock)
    */
    std::future<std::tuple<Descriptor, int>> connect(std::string const &aConnectAddress);

    /** Creates a new socket/pipe and attempts to connect to a remote address.
     *  'Returns' (via the supplied callback) the descriptor + error code of the listening pipe.
     *  This callback will be called on the IO thread
    */
    template<std::invocable<std::tuple<Descriptor, int>> callback_t>
    void connect(std::string const & aConnectAddress, callback_t && aCallback);

    /** Writes the supplied data to the pipe indentified by aPipeDescriptor.
     *
     *
    */
    template<requests::MessageableContainer container_t>
    std::future<int> write(Descriptor aPipeDescriptor, container_t &&aContainer);

    template<requests::MessageableContainer container_t,
        std::invocable<int> callback_t>
    void write(Descriptor aPipeDescriptor, container_t &&aContainer, callback_t &&aCallback);

private:
    void threadFunction(std::promise<void> aInitPromise);

    void requestStop();

    void onAsync(uv_async_t *aAsync);
    void onConnection(uv_stream_t* aServer, int aStatus); // incoming connection
    void onRead(uv_stream_t* aStream, ssize_t aNread, const uv_buf_t* aBuf);
    void onAlloc(uv_handle_t* aHandle, size_t aSuggested_size, uv_buf_t* aBuf);
    void onClosed(Descriptor aPipe, int aErrCode); // pipe closed
    void onConnect(uv_connect_t* aReq, int aStatus); // outgoing connection completed
    void onWrite(uv_write_t* aReq, int aStatus); // write completed

    void requireIOThread();
    void requireNonIOThread();

    Descriptor nextDescriptor();

    void triggerAsync();

    void postRequest(requests::Request::pointer);
    void processPendingRequests(bool aAbort);

    void handleListenRequest(requests::ListenRequest *) override;
    void handleConnectRequest(requests::ConnectRequest *) override;
    void handleWriteRequest(requests::WriteRequest *) override;

    void pipeRegister(UVPipe * aPipe);
    void pipeUnregister(Descriptor aDescriptor);
    UVPipe *pipeGet(Descriptor aDescriptor);


private:
    PiperDelegate::pointer  mDelegate { nullptr };
    std::thread             mIOThread;
    std::thread::id         mIOThreadId {};

    bool                    mStopFlag { false };
    uv_async_t              mAsyncTrigger {};
    std::mutex              mMx;

    Descriptor              mNextDescriptor { 1 }; // IO thread only
    std::unordered_map      <Descriptor, UVPipe*> mPipes; // IO thread only

    std::vector<requests::Request::pointer>  mPendingRequests;
    std::vector<requests::Request::pointer>  mPendingRequestsTemporary; // quickly switch these two under lock

    uv_loop_t               *mRunningLoop { nullptr }; // only accessed on the IO thread

};

//============================================================================================
// IMPLEMENTATION
//============================================================================================

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


template <std::invocable<std::tuple<Descriptor, int>> callback_t>
inline void Piper::listen(std::string const &aListenAddress, callback_t &&aCallback)
{
    postRequest(
        requests::makeListenRequest(aListenAddress, std::forward<callback_t>(aCallback))
    );
}


inline std::future<std::tuple<Descriptor, int>> Piper::connect(std::string const &aConnectAddress)
{
    requireNonIOThread();

    std::promise<std::tuple<Descriptor, int>> thePromise;
    auto ret_future = thePromise.get_future();

    postRequest(
        requests::makeConnectRequest(aConnectAddress, requests::promisingCallback(std::move(thePromise)))
    );

    return ret_future;
}


template <std::invocable<std::tuple<Descriptor, int>> callback_t>
inline void Piper::connect(std::string const &aConnectAddress, callback_t &&aCallback)
{
    postRequest(
        requests::makeConnectRequest(aConnectAddress, std::forward<callback_t>(aCallback))
    );
}

template <requests::MessageableContainer container_t>
inline std::future<int> Piper::write(Descriptor aPipeDescriptor, container_t &&aContainer)
{
    requireNonIOThread();
    std::promise<int> thePromise;
    auto ret_future = thePromise.get_future();

    postRequest(
        requests::makeWriteRequest(aPipeDescriptor,
            std::forward<container_t>(aContainer),
            requests::promisingCallback(std::move(thePromise)))
    );

    return ret_future;
}

template <requests::MessageableContainer container_t, std::invocable<int> callback_t>
inline void Piper::write(Descriptor aPipeDescriptor, container_t &&aContainer, callback_t &&aCallback)
{
    postRequest(
        requests::makeWriteRequest(aPipeDescriptor,
            std::forward<container_t>(aContainer),
            std::forward<callback_t>(aCallback)
        )
    );
}


}
