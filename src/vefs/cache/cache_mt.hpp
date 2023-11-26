#pragma once

#include <concepts>
#include <mutex>
#include <numeric>
#include <ranges>
#include <semaphore>
#include <span>
#include <thread>
#include <vector>

#include <dplx/predef/compiler.h>
#if defined(DPLX_COMP_MSVC_AVAILABLE)
#pragma warning(push, 3)
#pragma warning(disable : 4702) // unreachable code
#endif
#include <concurrentqueue/concurrentqueue.h>
#if defined(DPLX_COMP_MSVC_AVAILABLE)
#pragma warning(pop)
#endif

#include <boost/container/static_vector.hpp>
#include <dplx/cncr/intrusive_ptr.hpp>
#include <dplx/cncr/math_supplement.hpp>
#include <dplx/cncr/mp_lite.hpp>
#include <dplx/scope_guard.hpp>

#include <vefs/cache/cache_page.hpp>
#include <vefs/cache/eviction_policy.hpp>
#include <vefs/disappointment.hpp>
#include <vefs/utils/object_storage.hpp>
#include <vefs/utils/unordered_map_mt.hpp>

namespace vefs::detail
{

// clang-format off
template <typename P>
concept cache_traits
    =  std::constructible_from<P, typename P::initializer_type>
    && std::semiregular<typename P::key_type>
    && eviction_policy<typename P::eviction>
    && std::same_as<typename P::key_type, typename P::eviction::key_type>
    && requires(P &&p,
                typename P::key_type const &key,
                typename P::value_type &value,
                typename P::load_context &loadContext,
                typename P::purge_context &purgeContext,
                utils::object_storage<typename P::value_type> &storage)
{
    typename P::key_type;
    typename P::value_type;
    typename P::load_context;
    typename P::purge_context;
    typename P::allocator_type;
    { p.load(loadContext, key, storage) } noexcept
        -> tryable_result<std::pair<typename P::value_type *, bool>>;
    { p.sync(key, value) } noexcept
        -> tryable;
    { p.purge(purgeContext, key, value) } noexcept
        -> tryable;
};
// clang-format on

template <typename T, typename Alloc = std::allocator<T>>
class default_init_allocator : public Alloc
{
    using base_traits = std::allocator_traits<Alloc>;

public:
    template <typename U>
    struct rebind
    {
        using other = default_init_allocator<
                U,
                typename base_traits::template rebind_alloc<U>>;
    };

    using Alloc::Alloc;

    template <typename U>
    void construct(U *ptr) noexcept(std::is_nothrow_default_constructible_v<U>)
    {
        ::new (static_cast<void *>(ptr)) U;
    }
    template <typename U, typename... Args>
    void
    construct(U *ptr, Args &&...args) noexcept(noexcept(base_traits::construct(
            static_cast<Alloc &>(*this), ptr, std::forward<Args>(args)...)))
    {
        base_traits::construct(static_cast<Alloc &>(*this), ptr,
                               std::forward<Args>(args)...);
    }
};
template <typename Alloc>
using default_init_allocator_for
        = default_init_allocator<typename Alloc::value_type, Alloc>;

/**
 * @brief An associative fixed size key-value cache
 *
 * The cache only allocates memory on construction.
 *
 *
 */
template <cache_traits Traits>
class cache_mt
{
public:
    using traits_type = Traits;
    using eviction_policy = typename Traits::eviction;
    using key_type = typename Traits::key_type;
    using value_type = typename Traits::value_type;
    using allocator_type = typename Traits::allocator_type;
    using allocator_traits = std::allocator_traits<allocator_type>;

    using handle = cache_handle<key_type, value_type const>;
    using writable_handle = cache_handle<key_type, value_type>;

private:
    using index_type = typename eviction_policy::index_type;
    template <typename T>
    using allocator_for = typename allocator_traits::template rebind_alloc<T>;
    using page_state = cache_page_state<key_type>;
    using load_context = typename Traits::load_context;
    using purge_context = typename Traits::purge_context;
    using value_storage = utils::object_storage<value_type>;
    using value_storage_allocator
            = default_init_allocator<value_storage,
                                     allocator_for<value_storage>>;

    struct entry_info
    {
        index_type index;
        typename page_state::state_type generation;
    };
    struct access_record
    {
        key_type key;
        entry_info entry;
    };

    using key_index_map = utils::unordered_map_mt<
            key_type,
            entry_info,
            std_hash_for<spooky_v2_hash, key_type>,
            std::equal_to<void>,
            allocator_for<std::pair<key_type const, entry_info>>>;

    traits_type mTraits;
    key_index_map mIndex;
    std::vector<page_state, allocator_for<page_state>> mPageCtrl;
    std::vector<value_storage, value_storage_allocator> mPage;
    moodycamel::ConcurrentQueue<access_record> mAccessRecords;
    std::mutex mDeadPagesSync;
    std::atomic<index_type> mNumDeadPages;
    std::vector<index_type, allocator_for<index_type>> mDeadPages;
    index_type mDeadPageTarget;
    std::mutex mEvictionSync;
    eviction_policy mEvictionPolicy;

public:
    ~cache_mt() noexcept
    {
        if (!std::is_trivially_destructible_v<value_type>)
        {
            typename page_state::state_type gen;
            index_type numPinned = 0;
            index_type numPinnedPreviously;
            do
            {
                numPinnedPreviously = numPinned;
                numPinned = 0;

                for (std::size_t i = 0U, limit = mPageCtrl.size(); i < limit;
                     ++i)
                {
                    using enum cache_replacement_result;
                    switch (mPageCtrl[i].try_start_replace(gen))
                    {
                    case clean:
                    case dirty:
                        mPage[i].destroy();
                        [[fallthrough]];

                    case dead:
                        mPageCtrl[i].cancel_replace();
                        break;

                    case pinned:
                        numPinned += 1;
                        break;
                    }
                }
            }
            while (numPinned != numPinnedPreviously);

            if (numPinned > 0)
            {
                // external references held while cache ist destructing
                // or circular page references
                std::abort();
            }
        }
    }
    cache_mt(index_type cacheSize,
             typename traits_type::initializer_type traitsInitializer,
             allocator_type const &alloc = allocator_type())
        : mTraits(
                static_cast<decltype(traitsInitializer) &&>(traitsInitializer))
        , mIndex(derive_index_size(cacheSize), {}, {}, alloc)
        , mPageCtrl(cacheSize, alloc)
        , mPage(cacheSize, alloc)
        , mAccessRecords(
                  cacheSize, 0U, std::thread::hardware_concurrency() * 2U)
        , mDeadPagesSync()
        , mNumDeadPages(cacheSize)
        , mDeadPages(cacheSize, alloc)
        , mDeadPageTarget(std::thread::hardware_concurrency() * 2U)
        , mEvictionSync()
        , mEvictionPolicy(std::span(mPageCtrl), mPageCtrl.size(), alloc)
    {
        std::ranges::reverse_view deadPages(mDeadPages);
        std::iota(deadPages.begin(), deadPages.end(), index_type{});
    }

    auto size() const noexcept -> index_type
    {
        return static_cast<index_type>(mPageCtrl.size());
    }

    auto try_pin(key_type const &key) noexcept -> handle
    {
        entry_info entry;
        // do we know about key?
        if (!mIndex.find(key, entry))
        {
            return nullptr;
        }

        return try_acquire_entry(key, entry);
    }

    auto pin_or_load(load_context const &ctx, key_type const &key) noexcept
            -> result<handle>
    {
        using enum std::memory_order;
        using enum cache_replacement_result;
        handle h;

        bool found;
        entry_info entry;

        // do we know about key?
        found = mIndex.find(key, entry);

    retry:
        if (found)
        {
            // cached?
            // this also removes the reference created by the retry logic
            // _after_ trying to pin.
            h = try_acquire_entry(key, entry);
            if (h)
            {
                return h;
            }
        }

        // nope, aquire an initialization slot
        bool shouldEvictOne = acquire_page(entry);

        auto &ctrl = mPageCtrl[entry.index];
        auto &page = mPage[entry.index];

        [[maybe_unused]] auto const targetReplacementMode
                = ctrl.try_start_replace(entry.generation);
        assert(targetReplacementMode == dead);

        entry_info foundEntry;
        // try to broadcast where value for key will appear
        if (!mIndex.uprase_fn(
                    key,
                    [this, &h,
                     &foundEntry](entry_info const &indexEntry) noexcept {
                        // someone was faster than us
                        // forcefully reference the page in order to prevent its
                        // untimely unbecoming during the retry
                        // note that at this point we do not know aything about
                        // its contents and state
                        h = handle{dplx::cncr::intrusive_ptr_acquire(
                                           &mPageCtrl[indexEntry.index]),
                                   mPage[indexEntry.index].pointer()};
                        foundEntry = indexEntry;
                        return false;
                    },
                    entry))
        {
            ctrl.cancel_replace();
            release_page(entry.index);

            found = true;
            entry = foundEntry;
            goto retry;
        }
        dplx::scope_guard indexRollback
                = [this, &key, &entry, &ctrl]() noexcept {
                      if (entry.generation != page_state::invalid_generation)
                      {
                          mIndex.erase(key);
                          ctrl.cancel_replace();
                          release_page(entry.index);
                      }
                  };

        if (shouldEvictOne)
        {
            VEFS_TRY(evict_one(key, entry.index));
        }
        else
        {
            std::lock_guard evictionLock{mEvictionSync};
            mEvictionPolicy.insert(key, entry.index);
        }
        dplx::scope_guard evictionRollback = [this, &key, &entry]() noexcept {
            if (entry.generation != page_state::invalid_generation)
            {
                std::lock_guard evictionLock{mEvictionSync};
                mEvictionPolicy.on_purge(key, entry.index);
            }
        };

        VEFS_TRY(auto loaded, mTraits.load(ctx, key, page));

        ctrl.finish_replace(key);
        if (loaded.second)
        {
            ctrl.mark_dirty();
        }
        entry.generation = page_state::invalid_generation;
        return handle{dplx::cncr::intrusive_ptr_import(&ctrl), loaded.first};
    }

    auto purge(purge_context &ctx, key_type const &key) noexcept -> result<void>
    {
        entry_info entry;
        // do we know about key?
        if (!mIndex.find(key, entry))
        {
            return archive_errc::not_loaded;
        }

        if (!mPageCtrl[entry.index].try_acquire_wait(key, entry.generation))
        {
            return archive_errc::not_loaded;
        }
        return purge_impl(
                ctx, dplx::cncr::intrusive_ptr_import(&mPageCtrl[entry.index]),
                entry.index);
    }
    auto purge(purge_context &ctx, handle &&which) noexcept -> result<void>
    {
        if (!which)
        {
            return errc::invalid_argument;
        }
        auto const where = static_cast<index_type>(
                (reinterpret_cast<std::byte const *>(which.get())
                 - reinterpret_cast<std::byte const *>(mPage.data()))
                / sizeof(value_type));

        auto ctrl = dplx::cncr::intrusive_ptr_acquire(&mPageCtrl[where]);
        which = nullptr;

        auto purgeRx = purge_impl(ctx, std::move(ctrl), where);
        if (purgeRx.has_failure())
        {
            which = handle{std::move(ctrl), mPage[where].pointer()};
        }
        return purgeRx;
    }

    auto sync(handle const &which) noexcept -> result<void>
    {
        if (which.is_dirty())
        {
            which.mark_clean();
            if (auto &&rx
                = mTraits.sync(which.key(), const_cast<value_type &>(*which));
                !oc::try_operation_has_value(rx))
            {
                (void)which.as_writable();
                return oc::try_operation_return_as(
                        static_cast<decltype(rx) &&>(rx));
            }
        }
        return oc::success();
    }
    auto sync_all() noexcept -> result<bool>
    {
        using boost::container::static_vector;
        constexpr index_type chunkSize = 512;

        bool anyDirty = false;
        index_type const numPages = size();
        static_vector<handle, chunkSize> syncQueue;

        for (index_type i = 0; i < numPages;)
        {
            do
            {
                auto *const ctrl = &mPageCtrl[i];
                if (ctrl->try_acquire_wait())
                {
                    if (auto h = dplx::cncr::intrusive_ptr_import(ctrl);
                        ctrl->is_dirty())
                    {
                        syncQueue.push_back(
                                handle{std::move(h), mPage[i].pointer()});
                    }
                }
            }
            while (++i < numPages && syncQueue.size() < syncQueue.capacity());

            anyDirty = anyDirty || syncQueue.size() > 0U;
            for (auto &h : syncQueue)
            {
                VEFS_TRY(sync(h));
                h = handle{};
            }
            syncQueue.clear();
        }
        return anyDirty;
    }

private:
    auto try_acquire_entry(key_type const &key, entry_info entry) noexcept
            -> handle
    {
        auto *const ctrl = &mPageCtrl[entry.index];
        if (!ctrl->try_acquire_wait(key, entry.generation))
        {
            // damn, we _knew_ about key, but already forgot 🤯
            return nullptr;
        }
        auto h = dplx::cncr::intrusive_ptr_import(ctrl);

        // log access
        auto const accessRecorded
                = mAccessRecords.try_enqueue({.key = key, .entry = entry});
        if (auto const approxQueued = mAccessRecords.size_approx();
            !accessRecorded
            || (approxQueued > size() / 2U && approxQueued % 8U == 0U))
                [[unlikely]]
        {
            // replay accesses if
            //          the queue is somewhat full or overflowing
            //      and we got lucky with the lock
            if (std::unique_lock evictionLock{mEvictionSync, std::try_to_lock};
                evictionLock.owns_lock())
            {
                replay_access_records();
            }
        }

        // in any case we return a handle to the page
        return handle(std::move(h), mPage[entry.index].pointer());
    }

    auto purge_impl(purge_context &ctx,
                    dplx::cncr::intrusive_ptr<page_state> &&ctrl,
                    index_type const where) noexcept -> result<void>
    {
        if (!ctrl->try_start_purge())
        {
            return archive_errc::still_in_use;
        }

        if (auto &&purgeRx
            = mTraits.purge(ctx, ctrl->key(), mPage[where].value());
            !oc::try_operation_has_value(purgeRx))
        {
            ctrl->purge_cancel();
            return oc::try_operation_return_as(
                    static_cast<decltype(purgeRx) &&>(purgeRx));
        }
        mIndex.erase(ctrl->key());
        mPage[where].destroy();
        ctrl.release()->purge_finish();

        release_page(where);
        return oc::success();
    }

    auto evict_one(key_type const &key, index_type const where) noexcept
            -> result<void>
    {
        using enum cache_replacement_result;
        entry_info victim{};
        cache_replacement_result evictionMode = pinned;
        {
            std::lock_guard evictionLock{mEvictionSync};
            replay_access_records();

            for (auto it = mEvictionPolicy.begin(),
                      end = mEvictionPolicy.end();
                 it != end; ++it) // NOLINT
            {
                evictionMode = mEvictionPolicy.try_evict(
                        std::move(it), victim.index, victim.generation);

                // this cannot be part of the loop condition due to `it` being
                // invalidated by a successfull eviction
                if (evictionMode != pinned)
                {
                    break;
                }
            }
            if (evictionMode == pinned)
            {
                return archive_errc::still_in_use;
            }
            mEvictionPolicy.insert(key, where);
            if (evictionMode == clean)
            {
                mIndex.erase(mPageCtrl[victim.index].key());
                mPage[victim.index].destroy();
                mPageCtrl[victim.index].cancel_replace();
                release_page(victim.index);
                return oc::success();
            }
        }

        auto &ctrl = mPageCtrl[victim.index];
        auto &page = mPage[victim.index];

        assert(evictionMode == dirty);
        // FIXME: think about reinserting a failed eviction
        VEFS_TRY(mTraits.sync(ctrl.key(), page.value()));

        mIndex.erase(ctrl.key());
        page.destroy();
        ctrl.update_generation();
        ctrl.cancel_replace();
        release_page(victim.index);
        return oc::success();
    }

    /**
     * @brief acquires a dead page
     * @param page will be set to the acquired page index and its generation
     * @return true if one should evict another page
     */
    auto acquire_page(entry_info &entry) noexcept -> bool
    {
        using enum std::memory_order;
        auto numDeadPages = mNumDeadPages.load(acquire);
        for (;;)
        {
            if (numDeadPages == index_type{})
            {
                mNumDeadPages.wait(index_type{}, acquire);
                numDeadPages = mNumDeadPages.load(relaxed);
                // defend against ABA problems related to wait + load
                if (numDeadPages == index_type{})
                {
                    continue;
                }
            }

            if (mNumDeadPages.compare_exchange_weak(
                        numDeadPages, numDeadPages - 1U, acq_rel, acquire))
            {
                break;
            }
        }
        std::lock_guard deadPagesLock{mDeadPagesSync};
        entry.index = mDeadPages.back();
        mDeadPages.pop_back();
        return mDeadPages.size() < mDeadPageTarget;
    }
    void release_page(index_type which) noexcept
    {
        {
            std::lock_guard deadPagesLock{mDeadPagesSync};
            mDeadPages.push_back(which);
        }
        mNumDeadPages.fetch_add(1U, std::memory_order::release);
        mNumDeadPages.notify_one();
    }

    // assumes the caller owns mEvictionSync
    void replay_access_records() noexcept
    {
        constexpr std::size_t bulk = 128U;
        auto const maxDequeues = size() * 4U;

        access_record records[bulk];
        std::size_t numDequeued;
        for (std::size_t tries = 0U;
             tries < maxDequeues
             && 0U < (numDequeued
                      = mAccessRecords.try_dequeue_bulk(records, bulk));
             ++tries)
        {
            auto it = std::begin(records);
            auto const end = it + numDequeued;
            for (; it != end; ++it)
            {
                if (mPageCtrl[it->entry.index].contains(it->entry.generation,
                                                        it->key))
                {
                    (void)mEvictionPolicy.on_access(it->key, it->entry.index);
                }
            }
        }
    }

    static constexpr auto derive_index_size(unsigned limit) noexcept -> unsigned
    {
        using dplx::cncr::div_ceil;
        using dplx::cncr::round_up;

        // ((limit * 6 / 5) / 4) * 4
        // reserving 120% of slots needed rounded by the number of slots in a
        // bucket
        return round_up(div_ceil(limit, 5U) * 6U,
                        key_index_map::slot_per_bucket());
    }
};

} // namespace vefs::detail
