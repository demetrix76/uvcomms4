#pragma once

#include "pack.h"
#include <cstdlib>
#include <list>
#include <type_traits>
#include <concepts>
#include <tuple>
#include <iterator>
#include <algorithm>
#include <cassert>


namespace uvcomms4
{
    /** A buffer to be held by the Collector
    */
    class ReadBuffer
    {
    public:
        /** Constructor takes ownership of the data block;
         * Memory will be freed in destructor; it is supposed to have been allocated with ReadBuffer::memalloc
        */
        ReadBuffer(char *aData, std::size_t aSize) :
            mData(aData), mSize(aSize)
        {}

        ~ReadBuffer()
        {
            if(mData)
                ReadBuffer::memfree(mData);
        }

        ReadBuffer(ReadBuffer && aOther) noexcept :
            mData(aOther.mData), mSize(aOther.mSize)
        {
            aOther.mData = nullptr;
            aOther.mSize = 0;
            static_assert(!std::is_copy_constructible_v<ReadBuffer>);
            static_assert(!std::is_copy_assignable_v<ReadBuffer>);
        }

        ReadBuffer & operator = (ReadBuffer const &) = delete;

        char * data() noexcept { return mData; }
        std::size_t size() noexcept { return mSize; }

        // to keep things toghether
        static char *memalloc(std::size_t aSize)
        {
            return static_cast<char*>(std::malloc(aSize));
        }

        static void memfree(char * aData)
        {
            std::free(aData);
        }

    private:
        char       *mData { nullptr };
        std::size_t mSize { 0 };
    };


    static constexpr std::ptrdiff_t MORE_DATA = -1;
    static constexpr std::ptrdiff_t DATA_CORRUPT = -2;

    enum class CollectorStatus : unsigned
    {
        NoMessage,  // does not contain a full message
        HasMessage, // contains at least one full message
        Corrupt     // data corrupted
    };

    /*Collector is not required to use ReadBuffer: any type that satisfies CollectibleBuffer will do */
    template<typename buffer_t>
    concept CollectibleBuffer = std::is_nothrow_move_constructible_v<buffer_t> &&
        requires (buffer_t buffer) {
            { buffer.data() } -> std::convertible_to<char const*>;
            { buffer.size() } -> std::convertible_to<std::size_t>;
        };

    /** Accumulates read buffers and allows for extracting messages from the stream.
    Consists of:
        * a list of memory buffers
        * position in the first buffer

    Invariants: a new message always starts in the current buffer; after extracting a message, we always remove the preceding buffers
    and adjust the position accordingly. Current position always points to the current (possibly incomplete) message header.
    Messages (and even message headers) may span across buffer boundaries.
    */
    template<CollectibleBuffer buffer_t>
    class CollectorT
    {
    public:
        static constexpr std::size_t header_size = 8;

        /// append a new buffer
        void append(buffer_t && aBuffer);

        /// check if there's at least aSize bytes available ahead of the current position
        bool contains(std::size_t aSize) const;

        /** Returns the current message length (nonnegative value),
         * or MORE_DATA if there's less than `header_size` bytes available,
         * or DATA_CORRUPT if the data is corrupt, meaning we should drop this connection
        */
        std::ptrdiff_t messageLength(bool aAdvance = false);

        /// returns the current Collector status (has message/no message/corrupt)
        CollectorStatus status(); // not const because messageLength() may be mutable

        /** extracts the current message to the supplied iterator or conainer
         * (whichever type copyTo() supports)
        */
        template<typename dest_t>
        CollectorStatus extractMessageTo(dest_t && aDest);

        /** Get the current message if exists */
        template<typename container_t>
        std::tuple<CollectorStatus, container_t> getMessage();


        /** Copy aCount bytes to aDest;
         * if aAdvance, adjust the current position and delete the no longer needed buffers.
         * (does not advance the current position if there's not enough data)
         * Returns false if there's not enough data available (the available data is still written to the destination).
         * May throw if the destination iterator throws
        */
        template<std::output_iterator<char> iter_t>
        bool copyTo(iter_t aDest, std::size_t aCount, bool aAdvance);


        /** Copy aCount bytes to the destination container which is only required to provide push_back()
         * if aAdvance, adjust the current position and delete the no longer needed buffers.
         * Returns false if there's not enough data available (the available data is still written to the destination).
        * May throw if the destination iterator throws.
        * Effectively, appends data to the container rather than replaces.
        * If the container has .reserve() and .size(), calls reserve() to avoid extra reallocation
        */
        template<typename container_t>
            requires requires(container_t cont, char c) { { cont.push_back(c) };  }
        bool copyTo(container_t & aContainer, std::size_t aCount, bool aAdvance);

    private:


    private:
        std::list<buffer_t>   mBuffers;
        std::size_t           mPos { 0 };
    };

    /// The Default Collector type used by Streamer
    using Collector = CollectorT<ReadBuffer>;


    template <CollectibleBuffer buffer_t>
    void CollectorT<buffer_t>::append(buffer_t &&aBuffer)
    {
        mBuffers.emplace_back(std::forward<buffer_t>(aBuffer));
    }


    template <CollectibleBuffer buffer_t>
    inline bool CollectorT<buffer_t>::contains(std::size_t aSize) const
    {
        auto pBuf = mBuffers.begin();
        auto pos = mPos;

        while(aSize > 0)
        {
            if(pBuf == mBuffers.end())
                return false;

            auto remainder = pBuf->size() - pos;
            if(aSize <= remainder)
                return true;
            else
            {
                aSize -= remainder;
                pBuf++;
                pos = 0;
            }
        }
        return true;
    }


    template <CollectibleBuffer buffer_t>
    inline std::ptrdiff_t CollectorT<buffer_t>::messageLength(bool aAdvance)
    {
        char buffer[header_size];
        if(!copyTo(buffer, header_size, aAdvance)) // if there's not enough data, aAdvance will have no effect
            return MORE_DATA;

        std::uint32_t length = u32_unpack(buffer);
        std::uint32_t lenhash = u32_unpack(buffer + 4);

        if(length_hash(length) != lenhash)
            return DATA_CORRUPT; // if the data is corrupt, aAdvance is no longer of concern

        return length;
    }


    template <CollectibleBuffer buffer_t>
    inline CollectorStatus CollectorT<buffer_t>::status()
    {
        auto msglen = messageLength(false);
        switch(msglen)
        {
        case MORE_DATA:
            return CollectorStatus::NoMessage;
        case DATA_CORRUPT:
            return CollectorStatus::Corrupt;
        default:
            return contains(msglen + header_size) ? CollectorStatus::HasMessage : CollectorStatus::NoMessage;
        };
    }


    template <CollectibleBuffer buffer_t>
    template <typename dest_t>
    inline CollectorStatus CollectorT<buffer_t>::extractMessageTo(dest_t &&aDest)
    {
        if(auto st = status(); st != CollectorStatus::HasMessage)
            return st;

        auto size = messageLength(true);
        if(size < 0)
            return CollectorStatus::Corrupt; // we've just corrupted it if so

        return copyTo(aDest, size, true) ? CollectorStatus::HasMessage : CollectorStatus::Corrupt;
    }


    template <CollectibleBuffer buffer_t>
    template <typename container_t>
    inline std::tuple<CollectorStatus, container_t> CollectorT<buffer_t>::getMessage()
    {
        if(CollectorStatus st = status(); st == CollectorStatus::HasMessage)
        {
            container_t container;
            extractMessageTo(container);
            return { st, container };
        }
        else
            return { st, {} };
    }


    template <CollectibleBuffer buffer_t>
    template <std::output_iterator<char> iter_t>
    inline bool CollectorT<buffer_t>::copyTo(iter_t aDest, std::size_t aCount, bool aAdvance)
    {
        auto pBuf = mBuffers.begin();
        auto pos = mPos;
        while(aCount > 0)
        {
            if(pBuf == mBuffers.end())
                return false; // not enough data; no need to adjust the buffer position either

            auto remainder = pBuf->size() - pos;
            auto to_copy = std::min(aCount, remainder);
            aDest = std::copy(pBuf->data() + pos, pBuf->data() + pos + to_copy, aDest);
            aCount -= to_copy;
            if(to_copy < remainder)
                pos += to_copy;
            else
            {
                pos = 0;
                pBuf++;
            }
        }

        if(aAdvance)
        {
            mBuffers.erase(mBuffers.begin(), pBuf);
            mPos = pos;
        }

        return true;
    }


    template <CollectibleBuffer buffer_t>
    template <typename container_t>
        requires requires(container_t cont, char c) { { cont.push_back(c) };  }
    inline bool CollectorT<buffer_t>::copyTo(container_t &aContainer, std::size_t aCount, bool aAdvance)
    {
        if constexpr(requires (container_t cont, std::size_t sz) {
            { cont.size() } -> std::convertible_to<std::size_t>;
            { cont.reserve(sz) };
        })
        {
            aContainer.reserve(aContainer.size() + aCount);
        }
        return copyTo(std::back_inserter(aContainer), aCount, aAdvance);
    }

}