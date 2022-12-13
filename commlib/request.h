#pragma once

#include "commlib.h"
#include "pack.h"
#include <uv.h>
#include <string>
#include <memory>
#include <future>
#include <type_traits>
#include <tuple>

namespace uvcomms4::requests
{
    static_assert(std::is_signed_v<Descriptor>, "Descriptor type must be signed"); // no longer important though

    struct ListenRequest;
    struct ConnectRequest;
    struct WriteRequest;
    struct CloseRequest;

    struct RequestHandler
    {
        using pointer = std::unique_ptr<RequestHandler>;
        virtual void handleListenRequest(ListenRequest *) = 0;
        virtual void handleConnectRequest(ConnectRequest *) = 0;
        virtual void handleWriteRequest(WriteRequest *) = 0;
        virtual void handleCloseRequest(CloseRequest *) = 0;
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

        void dispatchToHandler(RequestHandler *aHandler) override
        {
            aHandler->handleListenRequest(this);
        }

        virtual void fulfill(retval_t) = 0;

        void abort() override
        {
            fulfill( {0, UV_ECANCELED} );
        }

    protected:
        ListenRequest(std::string const & aListenAddress):
            listenAddress(aListenAddress)
        {}
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

    protected:
        ConnectRequest(std::string const &aConnectAddress) :
            connectAddress(aConnectAddress)
        {
            uv_connect_req.data = this;
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


//====================================================================================================
// WriteRequest
//====================================================================================================

    namespace detail
    {
        template<typename T>
        inline constexpr bool value_sizeof_1()
        {
            return 1 == sizeof(*std::data(std::declval<T>()));
        }
    }

    /** Constrains to a contiguous container that has data(), size(); is nothrow-movable
     *  and whose contained data is of char size
    */
    template<typename T>
    concept MessageableContainer = requires (T c) {
        { *std::data(c) } -> std::convertible_to<char>;
        { std::size(c) } -> std::convertible_to<std::size_t>;
        { std::begin(c) } -> std::contiguous_iterator;
    }
    && detail::value_sizeof_1<T>()
    && std::is_nothrow_move_constructible_v<T>;


    struct WriteRequest: Request
    {
        using retval_t = int;
        static constexpr std::size_t header_size = 8;

        Descriptor pipeDescriptor { 0 };
        char header[header_size] { 0 };
        uv_write_t uv_write_request {};

        void dispatchToHandler(RequestHandler *aHandler) override
        {
            aHandler->handleWriteRequest(this);
        }

        virtual void fulfill(retval_t) = 0;

        void abort() override
        {
            fulfill(UV_ECANCELED);
        }

        virtual char const * data() const noexcept = 0;
        virtual std::size_t  size() const noexcept = 0;

    protected:
        WriteRequest(Descriptor aPipeDescriptor, std::size_t aMessageSize):
            pipeDescriptor(aPipeDescriptor)
        {
            uv_write_request.data = this;
            u32_pack(static_cast<std::uint32_t>(aMessageSize), header);
            u32_pack(static_cast<std::uint32_t>(length_hash(aMessageSize)), &header[4]);
        }

    };


    template<typename container_t, typename callback_t>
    struct WriteRequestImpl: WriteRequest
    {
        template<MessageableContainer cont_t,
            std::invocable<retval_t> fun_t>
        WriteRequestImpl(Descriptor aPipeDescriptor, cont_t &&aContainer, fun_t &&aCallback):
            WriteRequest(aPipeDescriptor, std::size(aContainer)),
            mContainer(std::forward<cont_t>(aContainer)),
            mCallback(std::forward<fun_t>(aCallback))
        {}

        void fulfill(retval_t aRetval) override
        {
            mCallback(aRetval);
        }

        char const * data() const noexcept override
        {
            return std::data(mContainer);
        }

        std::size_t  size() const noexcept override
        {
            return std::size(mContainer);
        }

        container_t mContainer;
        callback_t  mCallback;
    };


    template<MessageableContainer container_t,
            std::invocable<WriteRequest::retval_t> callback_t>
    inline std::unique_ptr<WriteRequest>
    makeWriteRequest(Descriptor aPipeDescriptor, container_t &&aContainer, callback_t &&aCallback)
    {
        return std::make_unique<WriteRequestImpl<
            std::decay_t<container_t>,
            std::decay_t<callback_t>  >>
            (aPipeDescriptor, std::forward<container_t>(aContainer), std::forward<callback_t>(aCallback));
    }


//====================================================================================================
// CloseRequest
//====================================================================================================

    struct CloseRequest: Request
    {
        using retval_t = int;

        Descriptor  descriptor {};

        void dispatchToHandler(RequestHandler *aHandler) override
        {
            aHandler->handleCloseRequest(this);
        }

        virtual void fulfill(retval_t) = 0;

        void abort() override
        {
            fulfill( UV_ECANCELED );
        }
    protected:
        CloseRequest(Descriptor aDescriptor):
            descriptor(aDescriptor)
        {}
    };

    template<typename callback_t>
    struct CloseRequestImpl: CloseRequest
    {
        template<std::invocable<CloseRequest::retval_t> fun_t>
        CloseRequestImpl(Descriptor aDescriptor, fun_t &&aCallback):
            CloseRequest(aDescriptor),
            mCallback(std::forward<fun_t>(aCallback))
        {}

        void fulfill(retval_t aRetval) override
        {
            mCallback(aRetval);
        }

        callback_t mCallback;
    };

    template<std::invocable<CloseRequest::retval_t> callback_t>
    inline std::unique_ptr<CloseRequest>
    makeCloseRequest(Descriptor aDescriptor, callback_t &&aCallback)
    {
        return std::make_unique<CloseRequestImpl<std::decay_t<callback_t>>>
            (aDescriptor, std::forward<callback_t>(aCallback));
    }


}
