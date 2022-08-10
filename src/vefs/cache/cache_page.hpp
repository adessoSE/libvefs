#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include <dplx/cncr/intrusive_ptr.hpp>
#include <dplx/predef/compiler.h>

namespace vefs::detail
{

/**
 * @brief Indicates whether a cache page replacement succeeded or why it failed.
 *
 * The enum values have been chosen in a way which is compatible with the dirt
 * and tombstone flags of the page state The fifth bit indicates success which
 * has been chosen in order to allow eyeballing the internal state in hex form.
 */
enum class [[nodiscard]] cache_replacement_result{
        /**
         * @brief failed; the page is currently used
         */
        pinned = 0b0'0000,
        /**
         * @brief succeeded; the page is unoccupied
         */
        dead = 0b1'0010,
        /**
         * @brief succeeded; the page is occupied and clean
         */
        clean = 0b1'0000,
        /**
         * @brief succeeded; the page is occupied and dirty/modified
         *
         * This indicates that changes need to be synchronized.
         */
        dirty = 0b1'0001,
};

#if defined(DPLX_COMP_MSVC_AVAILABLE)
#pragma warning(push)
#pragma warning(disable : 4324) // structure was padded due to alignas(32)
#endif

/**
 * @brief Lifecycle implementation for cache pages.
 *
 * A cache page can be in one of the following states:
 *   - dead
 *   - initializing/replacing (a.k.a. dirty_tombstone)
 *   - clean
 *   - dirty
 *   - pinned
 *
 * All pages start in the dead state i.e. they do not contain any content.
 * If the content is being replaced (after a @ref try_start_replace call) the
 * state transitions to initializing. It leaves said state by @ref
 * finish_replace or @ref cancel_replace in which case it becomes pinned or dead
 * respectively. If pinned it will prevent replacing the page. A pinned page can
 * be modified and marked dirty which means that the content needs to be
 * synchronized either while being pinned or while being replaced. The pinned
 * state is being determined by a non-zero reference counter.
 *
 * The state also consists of a generation counter which tracks the page
 * replacements. It is incremented each time a replacement begins to happen and
 * serves as an optimization during @ref try_acquire which will fail fast in
 * case of a generation mismatch. However, the implementation still checks the
 * page key for equality due to (potential) generation counter wrap arounds for
 * guaranteed correctness.
 *
 * @tparam Key is the type of the cache keys
 */
template <typename Key>
class alignas(32) cache_page_state
{
public:
    using state_type = std::uint32_t;
    using key_type = Key;

private:
    /**
     *   generation   dirty
     *   v              v
     * [ 14bit | 1bit | 1bit | 16bit ]
     *           ^             ^
     *       tombstone      ref ctr
     */
    mutable std::atomic<state_type> mValue;

    key_type mKey;

    static constexpr state_type one = 1;

    static constexpr int ref_ctr_offset = 0;
    static constexpr int ref_ctr_digits = 16;
    static constexpr state_type ref_ctr_mask = ((one << ref_ctr_digits) - 1U)
                                            << ref_ctr_offset;
    static constexpr state_type ref_ctr_one = one << ref_ctr_offset;

    static constexpr state_type dirt_flag = one
                                         << (ref_ctr_offset + ref_ctr_digits);
    static constexpr state_type tombstone_flag = dirt_flag << 1;
    // this serves as an exclusive lock
    static constexpr state_type dirty_tombstone = dirt_flag | tombstone_flag;

    static constexpr int generation_offset
            = ref_ctr_offset + ref_ctr_digits + 1 + 1;
    static constexpr int generation_digits = 14;
    static constexpr state_type generation_mask
            = ((one << generation_digits) - 1U) << generation_offset;
    static constexpr state_type generation_one = one << generation_offset;

public:
    ~cache_page_state() noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<key_type>)
        {
            if ((mValue.load(std::memory_order::acquire) & tombstone_flag)
                != 0U)
            {
                std::destroy_at(&key());
            }
        }
    }
    cache_page_state() noexcept
        : mValue(tombstone_flag)
        , mKey{}
    {
    }

    static constexpr state_type invalid_generation = one;

    [[nodiscard]] auto key() const noexcept -> key_type const &
    {
        return mKey;
    }

    [[nodiscard]] auto is_dead() const noexcept -> bool
    {
        auto const state = mValue.load(std::memory_order::acquire);
        return (state & dirty_tombstone) == tombstone_flag;
    }
    [[nodiscard]] auto is_dirty() const noexcept -> bool
    {
        return (mValue.load(std::memory_order::acquire) & dirt_flag) != 0U;
    }
    [[nodiscard]] auto is_pinned() const noexcept -> bool
    {
        return (mValue.load(std::memory_order::acquire) & ref_ctr_mask) != 0U;
    }
    void mark_dirty() noexcept
    {
        mValue.fetch_or(dirt_flag, std::memory_order::release);
    }
    void mark_clean() noexcept
    {
        mValue.fetch_and(~dirt_flag, std::memory_order::release);
    }

    [[nodiscard]] auto contains(state_type const expectedGeneration,
                                key_type const &expectedKey) const noexcept
            -> bool
    {
        auto const state = mValue.load(std::memory_order::acquire);
        return (state & (tombstone_flag | generation_mask))
                    == (expectedGeneration & generation_mask)
            && expectedKey == key();
    }

    auto try_acquire_wait() noexcept -> bool
    {
        using enum std::memory_order;

        auto state = mValue.fetch_add(ref_ctr_one, acq_rel);
        for (;;)
        {
            if ((state & dirty_tombstone) == dirty_tombstone)
            {
                mValue.wait(state, acquire);
                state = mValue.load(relaxed);
            }
            else if ((state & tombstone_flag) != 0U)
            {
                break;
            }
            else
            {
                return true;
            }
        }

        mValue.fetch_sub(ref_ctr_one, relaxed);
        return false;
    }

    /**
     * @brief Tries to acquire a page of unknown state.
     *
     * It ensures that the page contains the data associated with the given
     * generation and key. If successful the page is pinned. This method fails
     * if the page is currently being replaced.
     *
     * @return true if the page was alive and the generation and key match
     */
    [[nodiscard]] auto try_acquire(key_type const &expectedKey,
                                   state_type const expectedGeneration) noexcept
            -> bool
    {
        using enum std::memory_order;
        assert((expectedGeneration & generation_mask) == expectedGeneration);

        auto const state = mValue.fetch_add(ref_ctr_one, acq_rel);
        // we include the tombstone flag
        // => the condition is true if this is dead or locked
        // => we only access mKey if it isn't being written to
        if ((state & (generation_mask | tombstone_flag)) != expectedGeneration
            || expectedKey != mKey)
        {
            mValue.fetch_sub(one, relaxed);
            return false;
        }
        return true;
    }
    /**
     * @brief Tries to acquire a page of unknown state.
     *
     * It ensures that the page contains the data associated with the given
     * generation and key. It may block while the page is currently initializing
     * or replacing. If successful the page is pinned.
     *
     * @return true if the page was alive and the generation and key match
     */
    [[nodiscard]] auto
    try_acquire_wait(key_type const &expectedKey,
                     state_type const expectedGeneration) noexcept -> bool
    {
        using enum std::memory_order;
        assert((expectedGeneration & generation_mask) == expectedGeneration);

        auto state = mValue.fetch_add(ref_ctr_one, acq_rel);
        for (;;)
        {
            if ((state & dirty_tombstone) == dirty_tombstone)
            {
                auto const currentGeneration = state & generation_mask;
                // we check whether the generation matches. If it doesn't we can
                // fail _early_ without waiting
                if (currentGeneration != expectedGeneration
                    && currentGeneration + generation_one != expectedGeneration)
                {
                    break;
                }
                mValue.wait(state, acquire);
                state = mValue.load(relaxed);
            }
            else if ((state & (generation_mask | tombstone_flag))
                             != expectedGeneration
                     || expectedKey != mKey)
            {
                break;
            }
            else
            {
                return true;
            }
        }

        mValue.fetch_sub(ref_ctr_one, relaxed);
        return false;
    }
    void add_reference() const noexcept
    {
        mValue.fetch_add(ref_ctr_one, std::memory_order::relaxed);
    }
    void release() const noexcept
    {
        mValue.fetch_sub(ref_ctr_one, std::memory_order::release);
    }

    [[nodiscard]] auto try_start_replace(state_type &nextGeneration) noexcept
            -> cache_replacement_result
    {
        using enum std::memory_order;
        using enum cache_replacement_result;

        auto state = mValue.load(acquire);
        state_type next;
        do
        {
            if ((state & tombstone_flag) == 0U && (state & ref_ctr_mask) != 0U)
            {
                // pinned => not replacable
                return pinned;
            }

            // not dirty <=> no write back necessary
            // => we can immediately update the generation
            // note that we don't care about generation overflow as it will
            // wrap around as intended
            static_assert(generation_one == (dirt_flag << 2));
            next = (state + ((~state & dirt_flag) << 2)) | dirty_tombstone
                 | ref_ctr_one;
        } while (!mValue.compare_exchange_weak(state, next, acq_rel, acquire));

        nextGeneration = (state + generation_one) & generation_mask;
        return static_cast<cache_replacement_result>(
                0b1'0000U | ((state & dirty_tombstone) >> ref_ctr_digits));
    }
    /**
     * @brief Updates the generation counter after @ref try_start_replace
     *        returned dirty. Only call this after synchronizing the page.
     */
    void update_generation() noexcept
    {
        using enum std::memory_order;

        // note that we don't care about generation overflow as it will wrap
        // around as intended
        auto state = mValue.fetch_add(generation_one, release);

        if ((state & ref_ctr_mask) > 1U)
        {
            mValue.notify_all();
        }
    }
    /**
     * @brief Finishes a replacement and stores the key alongside the state.
     *        Must be called after @ref try_start_replace and pins the page.
     * @return the generation id of the current page state for usage with
     *         @ref try_acquire.
     */
    void finish_replace(key_type nextKey) noexcept
    {
        using enum std::memory_order;

        mKey = std::move(nextKey);
        auto const state = mValue.fetch_and(~dirty_tombstone, release);

        if ((state & ref_ctr_mask) > 1U)
        {
            mValue.notify_all();
        }
    }
    /**
     * @brief Aborts a replacement after which the page is marked dead.
     */
    void cancel_replace() noexcept
    {
        mKey = key_type{};

        auto const state = mValue.fetch_sub(dirt_flag | ref_ctr_one,
                                            std::memory_order::release);

        if ((state & ref_ctr_mask) > 1U)
        {
            mValue.notify_all();
        }
    }

    auto try_start_purge() noexcept -> bool
    {
        using enum std::memory_order;

        auto state = mValue.load(acquire);
        state_type next;
        do
        {
            if ((state & ref_ctr_mask) != (ref_ctr_one))
            {
                return false;
            }

            next = state | dirty_tombstone;
        } while (mValue.compare_exchange_weak(state, next, acq_rel, acquire));

        return true;
    }
    void purge_cancel() noexcept
    {
        // we cannot recover the dirt state, therefore err on the side of
        // caution and mark the page dirty
        auto const state = mValue.fetch_sub(tombstone_flag | ref_ctr_one,
                                            std::memory_order::release);

        if ((state & ref_ctr_mask) > 1U)
        {
            mValue.notify_all();
        }
    }
    void purge_finish() noexcept
    {
        mKey = key_type{};

        auto const state
                = mValue.fetch_add(generation_one - (dirt_flag | ref_ctr_one),
                                   std::memory_order::release);

        if ((state & ref_ctr_mask) > 1U)
        {
            mValue.notify_all();
        }
    }
};

#if defined(DPLX_COMP_MSVC_AVAILABLE)
#pragma warning(pop)
#endif

} // namespace vefs::detail

template <typename Key>
struct dplx::cncr::reference_counted_traits<
        vefs::detail::cache_page_state<Key>>
{
    using counter_type = void;

    static void add_reference(
            vefs::detail::cache_page_state<Key> const &v) noexcept
    {
        v.add_reference();
    }
    static void
    release(vefs::detail::cache_page_state<Key> const &v) noexcept
    {
        v.release();
    }
};
template <typename Key>
struct dplx::cncr::reference_counted_traits<
        vefs::detail::cache_page_state<Key> const>
    : dplx::cncr::reference_counted_traits<
              vefs::detail::cache_page_state<Key>>
{
};

namespace vefs::detail
{

template <typename Key, typename Value>
class cache_handle;

template <typename Key, typename V1, typename V2>
    requires std::same_as<std::remove_const_t<V1>, std::remove_const_t<V2>>
inline auto operator==(cache_handle<Key, V1> const &lhs,
                       cache_handle<Key, V2> const &rhs) noexcept -> bool;

template <typename Key, typename Value>
class cache_handle
    : private dplx::cncr::intrusive_ptr<Value, cache_page_state<Key>>
{
    using base_type = dplx::cncr::intrusive_ptr<Value, cache_page_state<Key>>;

    template <typename K, typename V1, typename V2>
        requires std::same_as<std::remove_const_t<V1>, std::remove_const_t<V2>>
    friend auto operator==(cache_handle<K, V1> const &lhs,
                           cache_handle<K, V2> const &rhs) noexcept -> bool;

public:
    ~cache_handle() noexcept
    {
        if (this->operator bool())
        {
            base_type::get_handle()->mark_dirty();
        }
    }
    cache_handle() noexcept = default;

    cache_handle(cache_handle const &) noexcept = default;
    auto operator=(cache_handle const &) noexcept -> cache_handle & = default;

    cache_handle(cache_handle &&) noexcept = default;
    auto operator=(cache_handle &&) noexcept -> cache_handle & = default;

    using base_type::base_type;
    using base_type::operator bool;
    using base_type::operator*;
    using base_type::operator->;
    using base_type::get;

    auto key() const noexcept -> Key const &
    {
        return base_type::get_handle()->key();
    }

    friend inline auto operator==(cache_handle const &lhs,
                                  std::nullptr_t) noexcept -> bool
    {
        return not lhs;
    }

    /**
     * Checks whether the referenced cache_page is marked as dirty.
     */
    auto is_dirty() const noexcept -> bool
    {
        return base_type::get_handle()->is_dirty();
    }

    friend inline void swap(cache_handle &lhs, cache_handle &rhs) noexcept
    {
        swap(static_cast<base_type &>(lhs), static_cast<base_type &>(rhs));
    }
};

template <typename Key, typename Value>
class cache_handle<Key, Value const>
    : private dplx::cncr::intrusive_ptr<Value const, cache_page_state<Key>>
{
    using base_type
            = dplx::cncr::intrusive_ptr<Value const, cache_page_state<Key>>;

    template <typename K, typename V1, typename V2>
        requires std::same_as<std::remove_const_t<V1>, std::remove_const_t<V2>>
    friend auto operator==(cache_handle<K, V1> const &lhs,
                           cache_handle<K, V2> const &rhs) noexcept -> bool;

public:
    cache_handle() noexcept = default;

    using base_type::base_type;
    using base_type::operator bool;
    using base_type::operator*;
    using base_type::operator->;
    using base_type::get;

    auto key() const noexcept -> Key const &
    {
        return base_type::get_handle()->key();
    }

    friend inline auto operator==(cache_handle const &lhs,
                                  std::nullptr_t) noexcept -> bool
    {
        return not lhs;
    }

    /**
     * Checks whether the referenced cache_page is marked as dirty.
     */
    auto is_dirty() const noexcept -> bool
    {
        return base_type::get_handle()->is_dirty();
    }
    /**
     * Clears the dirty bit of the referenced cache_page.
     */
    void mark_clean() const noexcept
    {
        base_type::get_handle()->mark_clean();
    }
    auto as_writable() &&noexcept -> cache_handle<Key, Value>
    {
        auto *const alias = const_cast<Value *>(get());
        return cache_handle<Key, Value>{
                static_cast<base_type &&>(*this).get_handle(), alias};
    }
    auto as_writable() const &noexcept -> cache_handle<Key, Value>
    {
        return cache_handle<Key, Value>{base_type::get_handle(),
                                        const_cast<Value *>(get())};
    }

    friend inline void swap(cache_handle &lhs, cache_handle &rhs) noexcept
    {
        swap(static_cast<base_type &>(lhs), static_cast<base_type &>(rhs));
    }
};

template <typename Key, typename V1, typename V2>
    requires std::same_as<std::remove_const_t<V1>, std::remove_const_t<V2>>
inline auto operator==(cache_handle<Key, V1> const &lhs,
                       cache_handle<Key, V2> const &rhs) noexcept -> bool
{
    return lhs.get() == rhs.get() && lhs.get_handle() == rhs.get_handle();
}

} // namespace vefs::detail
