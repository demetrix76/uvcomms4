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
    };


    struct Request
    {
        using pointer = std::unique_ptr<Request>;
        virtual ~Request() {}

        virtual void dispatchToHandler(RequestHandler * aHandler) = 0;

        virtual void abort() = 0;
    };



    struct ListenRequest : Request
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




//====================================================================================================
// ListenRequest implementation
//====================================================================================================

    template<typename callback_t>
    struct ListenRequestImpl : ListenRequest
    {
        template<std::invocable<ListenRequest::retval_t> fun_t>
        ListenRequestImpl(std::string const & aListenAddress, fun_t && aCallback) :
            ListenRequest(aListenAddress),
            mCallback(std::forward<fun_t>(aCallback))
        {}

        void fulfill(retval_t aRetval) override
        {
            //policy_t::fulfill(aDescriptor);
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




}
