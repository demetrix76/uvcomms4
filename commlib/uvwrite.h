#pragma once

#include <uv.h>
#include "pack.h"
#include "commlib.h"
#include <utility>
#include <future>
#include <memory>
#include <concepts>
#include <type_traits>


namespace uvcomms4::detail
{
    template<typename T>
    constexpr bool value_sizeof_1()
    {
        return 1 == sizeof(*std::data(std::declval<T>()));
    }
}

namespace uvcomms4
{
    template<typename T>
    concept MessageableContainer = requires (T c) {
        { *std::data(c) } -> std::convertible_to<char>;
        { std::size(c) } -> std::convertible_to<std::size_t>;
        { std::begin(c) } -> std::contiguous_iterator;
    }
    && detail::value_sizeof_1<T>()
    && std::is_nothrow_move_constructible_v<T>;


    template<typename fun_t>
    concept MessageSendCallback = std::invocable<fun_t, int>;
}

namespace uvcomms4::detail
{

struct CompletionPolicyPromise
{
    using promise_t = std::promise<int>;
    using future_t = std::future<int>;

    void complete_impl(int aRetCode)
    {
        mPromise.set_value(aRetCode);
    }

    future_t get_future() { return mPromise.get_future(); }

    promise_t   mPromise;
};


template<MessageSendCallback callback_t>
struct CompletionPolicyCallback
{
    template<MessageSendCallback fun_t>
    CompletionPolicyCallback(fun_t && aCallback) :
        mCallback(std::forward<fun_t>(aCallback))
    {}

    void complete_impl(int aRetCode)
    {
        mCallback(aRetCode);
    }

    callback_t mCallback;
};

template<MessageSendCallback fun_t>
CompletionPolicyCallback(fun_t &&) -> CompletionPolicyCallback<std::decay_t<fun_t>>;



class IWriteRequest
{
public:
    using pointer_t = std::unique_ptr<IWriteRequest>; // I guess we can't put this to uv_write_t

    IWriteRequest(Descriptor aPipeDescriptor, std::size_t aMessageSize) :
        pipeDescriptor(aPipeDescriptor)
    {
        // check that message length does not exceed std::uint32 max value; here or in the send() function?
        uv_write_request.data = this;
        u32_pack(static_cast<std::uint32_t>(aMessageSize), header);
        u32_pack(static_cast<std::uint32_t>(length_hash(aMessageSize)), &header[4]);
    }

    virtual ~IWriteRequest() {}

    virtual char const * data() const noexcept = 0;
    virtual std::size_t  size() const noexcept = 0;

    virtual void complete(int code) = 0;

    Descriptor pipeDescriptor;
    char header[8] {0};
    uv_write_t uv_write_request {};
};


template<MessageableContainer container_t, typename policy_t>
class WriteRequest : public IWriteRequest, public policy_t
{
public:
    template<MessageableContainer cont_t>
    WriteRequest(Descriptor aPipeDescriptor, cont_t && aContainer) :
        IWriteRequest(aPipeDescriptor, std::size(aContainer)),
        mContainer(std::forward<cont_t>(aContainer))
    {}

    template<MessageableContainer cont_t, typename fun_t>
    WriteRequest(Descriptor aPipeDescriptor, cont_t && aContainer, fun_t && aCallback) :
        IWriteRequest(aPipeDescriptor, std::size(aContainer)),
        policy_t(std::forward<fun_t>(aCallback)),
        mContainer(std::forward<cont_t>(aContainer))
    {}

    char const* data() const noexcept override
    {
        if(std::size(mContainer) > 0)
            return reinterpret_cast<char const*>(std::data(mContainer));
        else
            return nullptr; // maybe we need some real pointer to a dummy char to avoid confusing the low level API?
    }

    std::size_t size() const noexcept override
    {
        return std::size(mContainer);
    }

    void complete(int code) override
    {
        policy_t::complete_impl(code);
    }

private:
    container_t mContainer;

};

template<MessageableContainer cont_t>
WriteRequest(Descriptor aPipeDescriptor, cont_t &&) -> WriteRequest<std::decay_t<cont_t>, CompletionPolicyPromise>;

template<MessageableContainer cont_t, typename fun_t>
WriteRequest(Descriptor aPipeDescriptor, cont_t && aContainer, fun_t && aCallback) ->
    WriteRequest<std::decay_t<cont_t>,  CompletionPolicyCallback<std::decay_t<fun_t>>>;


// in desperate need of unique_ptr type deduction...
template<typename T>
inline std::unique_ptr<T> nonarray_unique_ptr(T* ptr)
{
    return std::unique_ptr<T>(ptr);
}


}