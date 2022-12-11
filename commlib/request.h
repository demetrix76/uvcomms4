#pragma once

#include "commlib.h"
#include <uv.h>
#include <string>
#include <memory>
#include <future>
#include <type_traits>
#include <tuple>

namespace uvcomms4::requests
{
    static_assert(std::is_signed_v<Descriptor>, "Descriptor type must be signed");

    struct ListenRequest;
    struct ConnectRequest;
    struct WriteRequest;

    struct RequestHandler
    {
        using pointer = std::unique_ptr<RequestHandler>;
        virtual void handleListenRequest(ListenRequest *) = 0;
        virtual void handleConnectRequest(ConnectRequest *) = 0;
    };


    struct Request
    {
        using pointer = std::unique_ptr<Request>;
        virtual ~Request() {}

        virtual void dispatchToHandler(RequestHandler * aHandler) = 0;

        virtual void abort() = 0;
    };


//====================================================================================================
// ListenRequest
//====================================================================================================

    struct ListenRequest: Request
    {
        using retval_t = std::tuple<Descriptor, int>;

        std::string listenAddress;

        ListenRequest(std::string const & aListenAddress):
            listenAddress(aListenAddress)
        {}

        void dispatchToHandler(RequestHandler *aHandler) override
        {
            aHandler->handleListenRequest(this);
        }

        virtual void fulfill(retval_t) = 0;

        void abort() override
        {
            fulfill( {0, UV_ECANCELED} );
        }
    };


    template<typename callback_t>
    struct ListenRequestImpl: ListenRequest
    {
        template<std::invocable<ListenRequest::retval_t> fun_t>
        ListenRequestImpl(std::string const & aListenAddress, fun_t && aCallback) :
            ListenRequest(aListenAddress),
            mCallback(std::forward<fun_t>(aCallback))
        {}

        void fulfill(retval_t aRetval) override
        {
            mCallback(aRetval);
        }

        callback_t mCallback;
    };


    template<std::invocable<ListenRequest::retval_t> callback_t>
    inline std::unique_ptr<ListenRequest>
    makeListenRequest(std::string const & aListenAddress, callback_t && aCallback)
    {
        return std::make_unique< ListenRequestImpl<std::decay_t<callback_t>>>
            (aListenAddress, std::forward<callback_t>(aCallback));
    }

    template<typename retval_t>
    auto promisingCallback(std::promise<retval_t> && aPromise)
    {
        return [pms = std::move(aPromise)] <typename T> (T && v) mutable {
            pms.set_value(std::forward<T>(v));
        };
    }

//====================================================================================================
// ConnectRequest
//====================================================================================================

    struct ConnectRequest: Request
    {
        using retval_t = std::tuple<Descriptor, int>;

        std::string connectAddress;
        uv_connect_t uv_connect_req {};

        ConnectRequest(std::string const &aConnectAddress) :
            connectAddress(aConnectAddress)
        {
            uv_connect_req.data = this;
        }

        static ConnectRequest* fromUVReq(uv_connect_t *aReq)
        {
            return static_cast<ConnectRequest*>(aReq->data);
        }

        void dispatchToHandler(RequestHandler *aHandler) override
        {
            aHandler->handleConnectRequest(this);
        }

        virtual void fulfill(retval_t) = 0;

        void abort() override
        {
            fulfill( {0, UV_ECANCELED} );
        }
    };


    template<typename callback_t>
    struct ConnectRequestImpl: ConnectRequest
    {
        template<std::invocable<ConnectRequest::retval_t> fun_t>
        ConnectRequestImpl(std::string const &aConnectAddress, fun_t &&aCallback):
            ConnectRequest(aConnectAddress),
            mCallback(std::forward<fun_t>(aCallback))
        {}

        void fulfill(retval_t aRetval) override
        {
            mCallback(aRetval);
        }

        callback_t mCallback;
    };


    template<std::invocable<ConnectRequest::retval_t> callback_t>
    inline std::unique_ptr<ConnectRequest>
    makeConnectRequest(std::string const & aConnectAddress, callback_t && aCallback)
    {
        return std::make_unique< ConnectRequestImpl<std::decay_t<callback_t>>>
            (aConnectAddress, std::forward<callback_t>(aCallback));
    }

}
