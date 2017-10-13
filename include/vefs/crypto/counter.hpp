#pragma once

#include <cstddef>

#include <array>
#include <mutex>
#include <stdexcept>

#include <vefs/blob.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto
{
    class counter
    {
        using guard_t = std::lock_guard<std::mutex>;

    public:
        using state = utils::secure_byte_array<16>;

        explicit counter(state ctrState);
        explicit counter(blob_view ctrState);

        state value() const noexcept;
        void increment();
        counter & operator++();
        counter & operator++(int);

        state fetch_increment();

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
            throw std::invalid_argument("ctr state size mismatch");
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
}
