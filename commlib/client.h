#pragma once

#include "Streamer.h"
#include "uvrequest.h"
#include <thread>
#include <future>
#include <memory>

namespace uvcomms4
{

class Client final: public Streamer<Client>
{
public:
    friend UVPipeT<Client>;
    friend Streamer<Client>;

    Client(config const & aConfig, ClientDelegate::pointer aDelegate);
    ~Client();

    // todo maybe add an assert to ensure this is not called on the IO thread?
    // may be called from any thread except the IO thread (may deadlock waiting on the future)
    std::future<detail::IConnectRequest::retval>
    connect(std::string const & aPipeName);

    // may be called from any thread; callback is called on the IO thread
    template <std::invocable<detail::IConnectRequest::retval> callback_t>
    void connect(std::string const &aPipeName, callback_t && aCallback);

    // TODO add disconnect()?

private:
    void threadFunction(std::promise<void> aInitPromise);

    void onAsync(uv_async_t * aAsync, bool aStopping);

    void onConnect(uv_connect_t* aReq, int aStatus);

    void initiateConnect(detail::IConnectRequest::pointer aReq, uv_loop_t * aLoop);

    void processPendingConnectRequests(uv_loop_t * aLoop);
    void abortPendingConnectRequests(uv_loop_t * aLoop);

private:
    std::thread mThread;

    std::vector<detail::IConnectRequest::pointer>  mConnectQueue;
    std::vector<detail::IConnectRequest::pointer>  mConnectQueueTemporary;
};


//==========================================================================================

inline std::future<detail::IConnectRequest::retval>
Client::connect(std::string const &aPipeName)
{
    auto conn_req = detail::nonarray_unique_ptr(new detail::ConnectRequest(aPipeName));
    auto ret_future = conn_req->get_future();
    {
        std::lock_guard lk(mMx);
        mConnectQueue.emplace_back(std::move(conn_req));
    }
    trigger_async();
    return ret_future;
}

template <std::invocable<detail::IConnectRequest::retval> callback_t>
inline void Client::connect(std::string const &aPipeName, callback_t &&aCallback)
{
    auto conn_req = detail::nonarray_unique_ptr(
        new detail::ConnectRequest(aPipeName, std::forward<callback_t>(aCallback)));
    {
        std::lock_guard lk(mMx);
        mConnectQueue.emplace_back(std::move(conn_req));
    }
    trigger_async();
}

}
