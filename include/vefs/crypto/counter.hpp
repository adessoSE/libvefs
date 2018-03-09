#pragma once

#include <cstddef>

#include <array>
#include <mutex>
#include <atomic>
#include <stdexcept>

#include <vefs/blob.hpp>
#include <vefs/exceptions.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto
{
    class counter
    {
    public:
        static constexpr std::size_t state_size = 16;
        using state = utils::secure_byte_array<state_size>;

        counter() = default;
        explicit counter(state ctrState);
        explicit counter(blob_view ctrState);

        const state & value() const noexcept;
        blob_view view() const noexcept;
        void increment();
        counter & operator++();
        counter operator++(int);

    private:
        alignas(std::size_t) state mCtrState;
    };

    inline counter::counter(state ctrState)
        : mCtrState(std::move(ctrState))
    {
    }

    inline counter::counter(blob_view ctrState)
        : mCtrState()
    {
        if (ctrState.size() != mCtrState.size())
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "ctrState" }
                << errinfo_param_misuse_description{ "ctr state size mismatch" }
            );
        }
        ctrState.copy_to(blob{ mCtrState });
    }

    inline const counter::state & counter::value() const noexcept
    {
        return mCtrState;
    }

    inline blob_view counter::view() const noexcept
    {
        return blob_view{ mCtrState };
    }

    inline counter& counter::operator++()
    {
        increment();
        return *this;
    }

    inline counter counter::operator++(int)
    {
        counter current{ *this };
        increment();
        return current;
    }

    inline bool operator==(const counter &lhs, const counter &rhs)
    {
        const auto &lhstate = lhs.value();
        const auto &rhstate = rhs.value();
        return std::equal(lhstate.cbegin(), lhstate.cend(), rhstate.cbegin());
    }
    inline bool operator!=(const counter &lhs, const counter &rhs)
    {
        return !(lhs == rhs);
    }
}

namespace std
{
    template <>
    struct atomic<vefs::crypto::counter>
    {
        using value_type = vefs::crypto::counter;

        atomic() noexcept
            : mImpl(value_type::state{})
            , mAccessMutex()
        {
        }
        atomic(value_type ctr) noexcept
            : mImpl(ctr)
            , mAccessMutex()
        {
        }
        atomic(value_type::state ctrState) noexcept
            : mImpl(ctrState)
            , mAccessMutex()
        {
        }
        atomic(vefs::blob_view ctrState) noexcept
            : mImpl(ctrState)
            , mAccessMutex()
        {
        }
        atomic(const atomic &) = delete;

        static constexpr bool is_lock_free() noexcept
        {
            return false;
        }
        static constexpr bool is_always_lock_free = false;

        void store(value_type desired, std::memory_order = std::memory_order_seq_cst) noexcept
        {
            guard_t lock{ mAccessMutex };
            mImpl = desired;
        }
        value_type load(std::memory_order = std::memory_order_seq_cst) const noexcept
        {
            guard_t lock{ mAccessMutex };
            return mImpl;
        }

        operator value_type() const noexcept
        {
            return load();
        }
        value_type operator=(value_type desired) noexcept
        {
            guard_t lock{ mAccessMutex };
            return mImpl = desired;
        }
        atomic & operator=(const atomic &) = delete;

        value_type exchange(value_type desired, std::memory_order = std::memory_order_seq_cst) noexcept
        {
            guard_t lock{ mAccessMutex };
            value_type old{ mImpl };
            mImpl = desired;
            return old;
        }
        bool compare_exchange_weak(const value_type &expected, value_type desired,
            std::memory_order = std::memory_order_seq_cst) noexcept
        {
            guard_t lock{ mAccessMutex };
            const auto success = mImpl == expected;
            if (success)
            {
                mImpl = desired;
            }
            return success;
        }
        bool compare_exchange_strong(const value_type &expected, value_type desired,
            std::memory_order = std::memory_order_seq_cst) noexcept
        {
            return compare_exchange_weak(expected, desired);
        }

        value_type fetch_increment() noexcept
        {
            guard_t lock{ mAccessMutex };
            return mImpl++;
        }
        value_type operator++() noexcept
        {
            guard_t lock{ mAccessMutex };
            return ++mImpl;
        }
        value_type operator++(int) noexcept
        {
            guard_t lock{ mAccessMutex };
            return mImpl++;
        }

    private:
        using guard_t = std::lock_guard<std::mutex>;

        vefs::crypto::counter mImpl;
        mutable std::mutex mAccessMutex;
    };
}

namespace vefs::crypto
{
    using atomic_counter = std::atomic<counter>;
}
