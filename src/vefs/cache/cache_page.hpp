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

namespace cache_ng
{

/**
 * @brief Indicates whether a cache page replacement succeeded or why it failed.
 */
enum class [[nodiscard]] cache_replacement_result{
        /**
         * @brief failed; the page is currently used
         */
        pinned = 0,
        /**
         * @brief succeeded; the page is unoccupied
         */
        dead,
        /**
         * @brief succeeded; the page is occupied and clean
         */
        clean,
        /**
         * @brief succeeded; the page is occupied and dirty/modified
         *
         * This indicates that changes need to be synchronized.
         */
        dirty,
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
    using state_type = std::uint32_t;
    using state_traits = std::numeric_limits<state_type>;
    using key_type = Key;

    /**
     *   tombstone   generation
     *   v              v
     * [ 1bit | 1bit | 14bit | 16bit ]
     *          ^               ^
     *        dirty          ref ctr
     */
    mutable std::atomic<state_type> mPageState;

    alignas(key_type) std::byte mKeyStorage[sizeof(key_type)];

    static constexpr state_type one = 1;

    static constexpr int ref_ctr_offset = 0;
    static constexpr int ref_ctr_digits = 16;
    static constexpr state_type ref_ctr_mask = ((one << ref_ctr_digits) - 1U)
                                            << ref_ctr_offset;

    static constexpr int generation_offset = ref_ctr_offset + ref_ctr_digits;
    static constexpr int generation_digits = 14;
    static constexpr state_type generation_mask
            = ((one << generation_digits) - 1U) << generation_offset;

    static constexpr state_type dirt_flag
            = one << (generation_offset + generation_digits);
    static constexpr state_type tombstone_flag = dirt_flag << 1;
    static constexpr state_type dirty_tombstone = dirt_flag | tombstone_flag;

public:
    ~cache_page_state() noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<key_type>)
        {
            if ((mPageState.load(std::memory_order::acquire) & tombstone_flag)
                != 0U)
            {
                std::destroy_at(&key());
            }
        }
    }
    cache_page_state() noexcept
        : mPageState(tombstone_flag)
    {
    }

    auto key() const noexcept -> key_type const &
    {
        return *std::launder(reinterpret_cast<key_type const *>(mKeyStorage));
    }

    auto is_dead() const noexcept -> bool
    {
        state_type state = mPageState.load(std::memory_order::acquire);
        return (state & dirty_tombstone) == tombstone_flag;
    }
    auto is_dirty() const noexcept -> bool
    {
        return (mPageState.load(std::memory_order::acquire) & dirt_flag) != 0U;
    }
    auto is_pinned() const noexcept -> bool
    {
        return (mPageState.load(std::memory_order::acquire) & ref_ctr_mask)
            != 0U;
    }
    void mark_dirty() noexcept
    {
        mPageState.fetch_or(dirt_flag, std::memory_order::release);
    }
    void mark_clean() noexcept
    {
        mPageState.fetch_and(~dirt_flag, std::memory_order::release);
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
    auto try_acquire(state_type const expectedGeneration,
                     key_type const &expectedKey) noexcept -> bool
    {
        using enum std::memory_order;
        assert((expectedGeneration & generation_mask) == expectedGeneration);

        state_type state = mPageState.fetch_add(one, acq_rel);
        if ((state & dirty_tombstone) == dirty_tombstone)
        {
            if ((state & generation_mask) != expectedGeneration)
            {
                return false;
            }
            mPageState.wait(state, acq_rel);
            return try_acquire(expectedGeneration, expectedKey);
        }
        if ((state & tombstone_flag) != 0U)
        {
            return false;
        }
        if ((state & generation_mask) != expectedGeneration
            || expectedKey != key())
        {
            mPageState.fetch_sub(one, release);
            return false;
        }
        return true;
    }
    void add_reference() const noexcept
    {
        mPageState.fetch_add(one << ref_ctr_offset, std::memory_order::relaxed);
    }
    void release() const noexcept
    {
        mPageState.fetch_sub(one << ref_ctr_offset, std::memory_order::release);
    }

    auto try_start_replace() noexcept -> cache_replacement_result
    {
        using enum std::memory_order;
        using enum cache_replacement_result;
        state_type state = mPageState.load(acquire);
        state_type next;
        do
        {
            if ((state & tombstone_flag) == 0U && (state & ref_ctr_mask) != 0U)
            {
                // pinned => not replacable
                return pinned;
            }

            if ((state & dirt_flag) == 0U)
            {
                // not dirty <=> no write back necessary
                // => we can immediately update the generation
                next = dirty_tombstone | increment_generation(state);
            }
            else
            {

                // note that this preserves generation and dirt
                next = tombstone_flag | state;
            }
        } while (!mPageState.compare_exchange_weak(state, next, acq_rel,
                                                   acquire));

        if ((state & tombstone_flag) != 0U)
        {
            return dead;
        }
        if constexpr (!std::is_trivially_destructible_v<key_type>)
        {
            std::destroy_at(&key());
        }
        return (state & dirt_flag) != 0U ? dirty : clean;
    }
    /**
     * @brief Updates the generation counter after @ref try_start_replace
     *        returned dirty. Only call this after synchronizing the page.
     */
    void update_generation() noexcept
    {
        state_type const state = mPageState.load(std::memory_order::acquire);
        mPageState.store(increment_generation(state),
                         std::memory_order::release);

        mPageState.notify_all();
    }
    /**
     * @brief Finishes a replacement and stores the key alongside the state.
     *        Must be called after @ref try_start_replace and pins the page.
     * @return the generation id of the current page state for usage with
     *         @ref try_acquire.
     */
    auto finish_replace(key_type nextKey) noexcept -> state_type
    {
        new (static_cast<void *>(mKeyStorage)) key_type(std::move(nextKey));

        state_type state = mPageState.load(std::memory_order::relaxed);
        state &= generation_mask;
        state |= one;
        mPageState.store(state, std::memory_order::release);

        mPageState.notify_all();
        return state & generation_mask;
    }
    /**
     * @brief Aborts a replacement after which the page is marked dead.
     */
    void cancel_replace() noexcept
    {
        mPageState.fetch_and(tombstone_flag | generation_mask,
                             std::memory_order::release);

        mPageState.notify_all();
    }

private:
    static auto increment_generation(state_type const state) noexcept
            -> state_type
    {
        state_type current = state & generation_mask;
        state_type next
                = (current + (one << generation_offset)) & generation_mask;
        return (state & ~generation_mask) | next;
    }
};

#if defined(DPLX_COMP_MSVC_AVAILABLE)
#pragma warning(pop)
#endif

} // namespace cache_ng

} // namespace vefs::detail

template <typename Key>
struct dplx::cncr::reference_counted_traits<
        vefs::detail::cache_ng::cache_page_state<Key>>
{
    using counter_type = void;

    static void add_reference(
            vefs::detail::cache_ng::cache_page_state<Key> const &v) noexcept
    {
        v.add_reference();
    }
    static void
    release(vefs::detail::cache_ng::cache_page_state<Key> const &v) noexcept
    {
        v.release();
    }
};
template <typename Key>
struct dplx::cncr::reference_counted_traits<
        vefs::detail::cache_ng::cache_page_state<Key> const>
    : dplx::cncr::reference_counted_traits<
              vefs::detail::cache_ng::cache_page_state<Key>>
{
};
