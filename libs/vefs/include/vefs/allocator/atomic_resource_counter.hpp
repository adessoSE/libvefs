#pragma once

#include <atomic>
#include <thread>
#include <limits>

namespace vefs::detail
{
    enum class resource_acquire_result
    {
        failure = 0,
        success = 1,
        do_init = -1,
    };

    enum class resource_release_result
    {
        success = 0,
        do_cleanup = -1,
    };

    enum class resource_is_initialized_t
    {
    };
    constexpr resource_is_initialized_t resource_is_initialized{};

    template <typename counter_type, counter_type LIMIT>
    class atomic_resource_counter
    {
        using value_type = counter_type;
        using value_type_limits = std::numeric_limits<value_type>;
        using atomic_type = std::atomic<value_type>;

        static constexpr value_type limit = LIMIT;
        static constexpr value_type uninitialized = value_type_limits::max();
        static constexpr value_type initializing = uninitialized - 1;
        static constexpr value_type deinitializing = initializing - 1;

        static_assert(limit < deinitializing);

    public:
        constexpr atomic_resource_counter() noexcept;
        constexpr atomic_resource_counter(resource_is_initialized_t) noexcept;

        [[nodiscard]] inline auto try_aquire_one() noexcept -> resource_acquire_result;
        [[nodiscard]] inline auto release_one(bool deinitOnZero) noexcept
            -> resource_release_result;

        inline void notify_initialized() noexcept;
        inline void notify_cleanup_done() noexcept;

    private:
        atomic_type mState;
    };

    template <typename counter_type, counter_type LIMIT>
    constexpr atomic_resource_counter<counter_type, LIMIT>::atomic_resource_counter() noexcept
        : mState{uninitialized}
    {
    }

    template <typename counter_type, counter_type LIMIT>
    constexpr atomic_resource_counter<counter_type, LIMIT>::atomic_resource_counter(
        resource_is_initialized_t) noexcept
        : mState{0}
    {
    }

    template <typename counter_type, counter_type LIMIT>
    inline auto atomic_resource_counter<counter_type, LIMIT>::try_aquire_one() noexcept
        -> resource_acquire_result
    {
        value_type value = mState.load(std::memory_order_acquire);
        value_type next;
        do
        {
            if (value == limit)
            {
                return resource_acquire_result::failure;
            }
            else if (value == uninitialized)
            {
                next = initializing;
            }
            else if (value == initializing)
            {
                if constexpr (limit == 1)
                {
                    return resource_acquire_result::failure;
                }
                else
                {
                    value = 1;
                    next = 2;
                    std::this_thread::yield();
                }
            }
            else if (value == deinitializing)
            {
                value = uninitialized;
                next = initializing;
                std::this_thread::yield();
            }
            else
            {
                next = value + 1;
            }

        } while (!mState.compare_exchange_weak(value, next, std::memory_order_acq_rel,
                                               std::memory_order_acquire));

        return next == initializing ? resource_acquire_result::do_init
                                    : resource_acquire_result::success;
    }

    template <typename counter_type, counter_type LIMIT>
    inline auto
    atomic_resource_counter<counter_type, LIMIT>::release_one(bool deinitOnZero) noexcept
        -> resource_release_result
    {
        if (mState.fetch_sub(1, std::memory_order_release) == 1 && deinitOnZero)
        {
            value_type exp = 0;
            if (mState.compare_exchange_strong(exp, deinitializing, std::memory_order_acq_rel))
            {
                return resource_release_result::do_cleanup;
            }
        }
        return resource_release_result::success;
    }

    template <typename counter_type, counter_type LIMIT>
    inline void atomic_resource_counter<counter_type, LIMIT>::notify_initialized() noexcept
    {
        mState.store(1, std::memory_order_release);
    }

    template <typename counter_type, counter_type LIMIT>
    inline void atomic_resource_counter<counter_type, LIMIT>::notify_cleanup_done() noexcept
    {
        mState.store(uninitialized, std::memory_order_release);
    }
} // namespace vefs::detail
