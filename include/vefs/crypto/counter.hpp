#pragma once

#include <cstddef>

#include <array>
#include <mutex>
#include <stdexcept>

#include <vefs/blob.hpp>
#include <vefs/exceptions.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto
{
    class counter
    {
        using guard_t = std::lock_guard<std::mutex>;

    public:
        using state = utils::secure_byte_array<16>;

        counter() = default;
        explicit counter(state ctrState);
        explicit counter(blob_view ctrState);

        state value() const noexcept;
        void increment();
        counter & operator++();
        counter & operator++(int);

        state fetch_increment();

        void assign(state ctrState);
        void assign(blob_view ctrState);

    private:
        state mCtrState;
        mutable std::mutex mAccessMutex;
    };

    inline counter::counter(state ctrState)
        : mCtrState(std::move(ctrState))
        , mAccessMutex()
    {
    }

    inline counter::counter(blob_view ctrState)
        : mCtrState()
        , mAccessMutex()
    {
        if (ctrState.size() != mCtrState.size())
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument("ctr state size mismatch"));
        }
        ctrState.copy_to(blob{ mCtrState });
    }

    inline counter::state counter::value() const noexcept
    {
        guard_t sync{ mAccessMutex };
        return mCtrState;
    }

    inline counter& counter::operator++()
    {
        increment();
        return *this;
    }
    inline counter& counter::operator++(int)
    {
        increment();
        return *this;
    }

    inline void counter::assign(state ctrState)
    {
        mCtrState = ctrState;
    }
    inline void counter::assign(blob_view ctrState)
    {
        guard_t sync{ mAccessMutex };
        if (ctrState.size() != mCtrState.size())
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument("ctr state size mismatch"));
        }
        ctrState.copy_to(blob{ mCtrState });
    }
}
