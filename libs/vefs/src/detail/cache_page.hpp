#pragma once

#include <cstddef>
#include <cstdint>

#include <atomic>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

#include <vefs/utils/enum_bitset.hpp>

namespace vefs::detail
{
    /**
     * Indicates whether a cache page replacement succeeded or why it failed.
     */
    enum class cache_replacement_result
    {
        succeeded = 0,
        referenced = 0b0001,
        second_chance = 0b0010,
        dirty = 0b0100,

    };
    std::true_type allow_enum_bitset(cache_replacement_result);

    template <typename T>
    class cache_handle;

    template <typename T>
    struct cache_page
    {
        using state_type = std::uint64_t;
        using state_traits = std::numeric_limits<state_type>;

    private:
        static constexpr state_type one = 1;

        /** indicates that the cache entry is not alive */
        static constexpr state_type tombstone_bit = one << (state_traits::digits - 1);
        /**
         * indicates that the entry has been modified and needs synchronization
         * i.e. the entry is not available for replacement even though no active references exist
         */
        static constexpr state_type dirty_bit = tombstone_bit >> 1;
        /**
         * indicates that someone is currently initializing this entry
         * i.e. it's dead (= tombstone) but is not available (= dirty)
         */
        static constexpr state_type dirty_tombstone = tombstone_bit | dirty_bit;
        /**
         * if this bit is set a second chance must be granted (regardless of the ref count)
         */
        static constexpr state_type second_chance_bit = dirty_bit >> 1;
        /**
         * the remaining bits are used for reference counting
         */
        static constexpr state_type ref_mask = ~(tombstone_bit | dirty_bit | second_chance_bit);

    public:
        /**
         * constructs a new dead cache page
         */
        // #Todo why not remove constexpr comment?
        /*constexpr*/ cache_page() noexcept = default;
        ~cache_page() noexcept;

        /**
         * tries to replace the value stored in this page
         *
         * If successful the replacement will need to be completed by calling finish_replace()
         * or cancel_replace().
         * The replacement will fail if the page is dirty, referenced or has the second chance bit
         * set.
         */
        inline auto try_start_replace() noexcept -> enum_bitset<cache_replacement_result>;
        /**
         * Completes page replacement by constructing the new element in place.
         * If construction fails its effects are equivalent to cancel_replace().
         *
         * \param ctor The in place element construction function. result<value_type
         * *>|outcome<value_type *> ctor(void *) noexcept \returns A page handle if successful or
         * any error produced by the construction function.
         */
        template <typename Ctor>
        inline auto finish_replace(Ctor &&ctor) noexcept ->
            typename std::invoke_result_t<Ctor, void *>::template rebind<cache_handle<T>>;
        /**
         * Completes replacement by marking this page as dead.
         */
        inline void cancel_replace() noexcept;

        /**
         * tries to destruct the current page
         */
        inline bool try_purge(bool ownsLastReference) noexcept;

        inline bool is_dead() const noexcept;
        /**
         * acquires a handle to the current page and sets the second chance bit
         */
        inline auto try_acquire() noexcept -> cache_handle<T>;
        /**
         * acquires a handle to the current page without setting the second chance bit
         */
        inline auto try_peek() noexcept -> cache_handle<T>;

        inline bool is_dirty() const noexcept;
        /**
         * sets the dirty bit to one
         * /return the previous dirty state
         */
        inline bool mark_dirty() noexcept;
        /**
         * sets the dirty bit to zero
         * /return the previous dirty state
         */
        inline bool mark_clean() noexcept;

        inline void add_reference() noexcept;
        inline void release() noexcept;

    private:
        inline bool try_add_reference() noexcept;
        inline auto make_handle() noexcept -> cache_handle<T>;

        std::atomic<state_type> mEntryState{
            tombstone_bit};
        std::aligned_storage_t<sizeof(T), alignof(T)> mValueHolder;
    };

    template <typename T>
    inline cache_page<T>::~cache_page() noexcept
    {
        //#TODO const?
        auto state = mEntryState.load(std::memory_order_acquire);
        if (!(state & tombstone_bit) && (state & ref_mask) > 0)
        {
            // #TODO maybe log something about open cache page references on destruction
            std::terminate();
        }
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            if ((state & tombstone_bit) == 0)
            {
                std::destroy_at(std::launder(reinterpret_cast<T *>(&mValueHolder)));
            }
        }
    }

    template <typename T>
    inline auto cache_page<T>::try_start_replace() noexcept -> enum_bitset<cache_replacement_result>
    {
        state_type current = mEntryState.fetch_and(~second_chance_bit, std::memory_order_acq_rel);
        if (current & second_chance_bit)
        {
            // respect second chance
            return current & dirty_bit
                       ? cache_replacement_result::second_chance | cache_replacement_result::dirty
                       : cache_replacement_result::second_chance;
        }

        do
        {
            // we only allow replacement if this state is zero or
            // it is a non dirty tombstone
            if (!(current == 0 || (current & tombstone_bit && !(current & dirty_bit))))
            {
                // notify the owner if this entry is not referenced and dirty but not dead
                // which usually is a good time to consider synchronizing this entry
                return current == dirty_bit ? cache_replacement_result::dirty
                                            : cache_replacement_result::referenced;
            }

        } while (!mEntryState.compare_exchange_weak(
            current, dirty_tombstone, std::memory_order_acq_rel, std::memory_order_acquire));

        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            if (!(current & tombstone_bit))
            {
                std::destroy_at(std::launder(reinterpret_cast<T *>(&mValueHolder)));
            }
        }
        return cache_replacement_result::succeeded;
    }
    template <typename T>
    template <typename Ctor>
    inline auto cache_page<T>::finish_replace(Ctor &&ctor) noexcept ->
        typename std::invoke_result_t<Ctor, void *>::template rebind<cache_handle<T>>
    {
        static_assert(std::is_nothrow_invocable_v<Ctor, void *>);
        auto rx = std::invoke(std::forward<Ctor>(ctor), static_cast<void *>(&mValueHolder));

        using result_type =
            typename std::invoke_result_t<Ctor, void *>::template rebind<cache_handle<T>>;
        if constexpr (can_result_contain_failure_v<result_type>)
        {
            if (rx.has_failure())
            {
                cancel_replace();
                return std::move(rx).as_failure();
            }
        }
        mEntryState.store(one, std::memory_order_release);
        return cache_handle<T>(std::move(rx).assume_value(), {this, utils::ref_ptr_import});
    }

    template <typename T>
    inline void cache_page<T>::cancel_replace() noexcept
    {
        mEntryState.store(tombstone_bit, std::memory_order_release);
    }

    template <typename T>
    inline bool cache_page<T>::try_purge(bool ownsLastReference) noexcept
    {
        const state_type remainingReferences = ownsLastReference ? 1 : 0;
        state_type expected = remainingReferences;
        while (!mEntryState.compare_exchange_strong(expected, dirty_tombstone,
                                                 std::memory_order_acq_rel))
        {
            if (expected != (remainingReferences | second_chance_bit) &&
                expected != remainingReferences)
            {
                return false;
            }
        }

        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            std::destroy_at(std::launder(reinterpret_cast<T *>(&mValueHolder)));
        }

        // if we still own the last reference we will decrement once afterwards
        mEntryState.store(tombstone_bit | remainingReferences, std::memory_order_release);
        return true;
    }

    template <typename T>
    inline bool cache_page<T>::is_dead() const noexcept
    {
        return mEntryState.load(std::memory_order_acquire) & tombstone_bit;
    }
    template <typename T>
    inline auto cache_page<T>::try_acquire() noexcept -> cache_handle<T>
    {
        if (try_add_reference())
        {
            mEntryState.fetch_or(second_chance_bit, std::memory_order_release);
            return make_handle();
        }
        return {};
    }
    template <typename T>
    inline auto cache_page<T>::try_peek() noexcept -> cache_handle<T>
    {
        if (try_add_reference())
        {
            return make_handle();
        }
        return {};
    }

    template <typename T>
    inline bool cache_page<T>::is_dirty() const noexcept
    {
        return mEntryState.load(std::memory_order_acquire) & dirty_bit;
    }
    template <typename T>
    inline bool cache_page<T>::mark_dirty() noexcept
    {
        return mEntryState.fetch_or(dirty_bit, std::memory_order_acq_rel) & dirty_bit;
    }
    template <typename T>
    inline bool cache_page<T>::mark_clean() noexcept
    {
        return !(mEntryState.fetch_and(~dirty_bit, std::memory_order_acq_rel) & dirty_bit);
    }

    template <typename T>
    inline void cache_page<T>::add_reference() noexcept
    {
        mEntryState.fetch_add(1, std::memory_order_release);
    }
    template <typename T>
    inline void cache_page<T>::release() noexcept
    {
        mEntryState.fetch_sub(1, std::memory_order_release);
    }

    template <typename T>
    inline bool cache_page<T>::try_add_reference() noexcept
    {
        return !(mEntryState.fetch_add(1, std::memory_order_acq_rel) & tombstone_bit);
    }
    template <typename T>
    inline auto cache_page<T>::make_handle() noexcept -> cache_handle<T>
    {
        return {std::launder(reinterpret_cast<T *>(&mValueHolder)), {this, utils::ref_ptr_import}};
    }
} // namespace vefs::detail
