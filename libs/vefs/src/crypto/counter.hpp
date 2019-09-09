#pragma once

#include <cassert>
#include <cstddef>

#include <array>
#include <atomic>
#include <mutex>
#include <stdexcept>

#include <vefs/span.hpp>
#include <vefs/exceptions.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto
{
    class counter
    {
    public:
        static constexpr std::size_t state_size = 16;
        using state = utils::secure_array<std::size_t, state_size / sizeof(std::size_t)>;

        counter() = default;
        explicit counter(state ctrState);
        explicit counter(ro_blob<state_size> ctrState);

        const state &value() const noexcept;
        auto view() const noexcept -> ro_blob<state_size>;
        void increment();
        counter &operator++();
        counter operator++(int);

    private:
        state mCtrState;
    };

    inline counter::counter(state ctrState)
        : mCtrState(std::move(ctrState))
    {
    }

    inline counter::counter(ro_blob<state_size> ctrState)
        : mCtrState()
    {
        copy(ctrState, as_writable_bytes(as_span(mCtrState)));
    }

    inline const counter::state &counter::value() const noexcept
    {
        return mCtrState;
    }

    inline auto counter::view() const noexcept -> ro_blob<state_size>
    {
        return as_bytes(as_span(mCtrState));
    }

    inline counter &counter::operator++()
    {
        increment();
        return *this;
    }

    inline counter counter::operator++(int)
    {
        counter current{*this};
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
} // namespace vefs::crypto

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
        atomic(vefs::ro_blob<value_type::state_size> ctrState) noexcept
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
            std::lock_guard lock{mAccessMutex};
            mImpl = desired;
        }
        value_type load(std::memory_order = std::memory_order_seq_cst) const noexcept
        {
            std::lock_guard lock{mAccessMutex};
            return mImpl;
        }

        operator value_type() const noexcept
        {
            return load();
        }
        value_type operator=(value_type desired) noexcept
        {
            std::lock_guard lock{mAccessMutex};
            return mImpl = desired;
        }
        atomic &operator=(const atomic &) = delete;

        value_type exchange(value_type desired, std::memory_order = std::memory_order_seq_cst) noexcept
        {
            std::lock_guard lock{mAccessMutex};
            value_type old{mImpl};
            mImpl = desired;
            return old;
        }
        bool compare_exchange_weak(const value_type &expected, value_type desired,
                                   std::memory_order = std::memory_order_seq_cst) noexcept
        {
            std::lock_guard lock{mAccessMutex};
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
            std::lock_guard lock{mAccessMutex};
            return mImpl++;
        }
        value_type operator++() noexcept
        {
            std::lock_guard lock{mAccessMutex};
            return ++mImpl;
        }
        value_type operator++(int) noexcept
        {
            std::lock_guard lock{mAccessMutex};
            return mImpl++;
        }

    private:
        vefs::crypto::counter mImpl;
        mutable std::mutex mAccessMutex;
    };
} // namespace std

namespace vefs::crypto
{
    using atomic_counter = std::atomic<counter>;
}
