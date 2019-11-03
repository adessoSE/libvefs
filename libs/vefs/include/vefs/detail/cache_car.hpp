#pragma once

#include <cstddef>
#include <cstdint>

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>

#include <boost/container/static_vector.hpp>

#include <vefs/allocator/system.hpp>
#include <vefs/blob.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/unordered_map_mt.hpp>

#include <vefs/detail/cache_clock.hpp>
#include <vefs/detail/cache_handle.hpp>
#include <vefs/detail/cache_page.hpp>

namespace vefs::detail
{
    /**
     * An associative cache implementation using an adapted CAR policy.
     */
    template <typename Key, typename T, unsigned int CacheSize,
              typename Hash = utils::hash::default_weak_std<Key>,
              typename KeyEqual = std::equal_to<>>
    class cache_car
    {
    public:
        static constexpr std::size_t max_entries = CacheSize;

        using key_type = Key;
        using value_type = T;

        using handle = cache_handle<value_type>;
        using notify_dirty_fn = std::function<void(handle)>;

    private:
        using clock = cache_clock<max_entries>;
        using page_type = cache_page<value_type>;
        using page_index = std::size_t;

        using page_allocator_type = system_allocator<alignof(page_type)>;
        static constexpr std::size_t page_allocation_size = sizeof(page_type) * max_entries;
        struct page_deleter
        {
            explicit page_deleter(page_allocator_type *allocator)
                : mAllocator(allocator)
            {
            }

            void operator()(page_type *memory)
            {
                std::destroy_n(memory, max_entries);
                mAllocator->deallocate(
                    {reinterpret_cast<std::byte *>(memory), page_allocation_size});
            }

        private:
            page_allocator_type *mAllocator;
        };
        using page_owner_type = std::unique_ptr<page_type, page_deleter>;
        static auto allocate(page_allocator_type &allocator) -> page_owner_type
        {
            if (auto allocrx = allocator.allocate(page_allocation_size))
            {
                auto memory = allocrx.assume_value();
                assert(memory.size() == max_entries * sizeof(page_type));
                static_assert(sizeof(page_type) % alignof(page_type) == 0);

                for (auto remaining = memory.writeable_bytes(); remaining;
                     remaining = remaining.subspan(sizeof(page_type)))
                {
                    new (static_cast<void *>(remaining.data())) page_type();
                }

                return page_owner_type{std::launder(reinterpret_cast<page_type *>(memory.raw())),
                                       page_deleter{&allocator}};
            }
            else
            {
                throw std::bad_alloc();
            }
        }

        static constexpr page_index invalid_page_index_bit =
            page_index(1) << (std::numeric_limits<page_index>::digits - 1);

        inline auto pages() noexcept -> span<page_type, max_entries>;
        inline auto page(std::size_t id) noexcept -> page_type &;

        using history_list = boost::container::static_vector<key_type, max_entries + 1>;
        using key_index_map = utils::unordered_map_mt<key_type, page_index, Hash, KeyEqual>;
        using index_key_map = std::array<key_type, max_entries>;

        static constexpr std::size_t derive_key_index_map_size(std::size_t limit)
        {
            // ((limit * 8 / 5) / 4) * 4
            // reserving 160% of slots needed rounded by the number of slots in a bucket
            return utils::div_ceil(utils::div_ceil(limit * 8, 5), 4) * 4;
        }

    public:
        inline cache_car(notify_dirty_fn fn);
        inline ~cache_car();

        /**
         * Tries to access the element. If it doesn't exist it returns a nullptr handle.
         */
        [[nodiscard]] inline auto try_access(const key_type &key) -> handle;

        /**
         * \param key the cache entry to look up / construct
         * \param ctor the element construction function, needs to return a result or outcome type
         */
        template <typename Ctor>
        inline auto access(const key_type &key, Ctor &&ctor) noexcept ->
            typename std::invoke_result_t<Ctor, void *>::template rebind<handle>;
        /**
         * \param key the cache entry to look up / construct
         * \param ctor the element construction function, needs to return a result or outcome type
         * \param inserted indicates whether or not the element was constructed using ctor.
         */
        template <typename Ctor>
        inline auto access(const key_type &key, Ctor &&ctor, bool &inserted) noexcept ->
            typename std::invoke_result_t<Ctor, void *>::template rebind<handle>;

        /**
         * Either acquires a page handle or constructs it inplace using the value_type constructor
         *
         * If the constructor is noexcept, this function will not fail and just return a handle.
         * Otherwise it catches any exception thrown and stores it in an outcome.
         */
        template <typename... Args>
        inline auto access(const key_type &key, Args &&... ctorArgs) noexcept
            -> std::conditional_t<std::is_nothrow_constructible_v<value_type, Args...>, handle,
                                  op_outcome<handle>>;

        /**
         * Calls for_dirty with the stored dirty page handler.
         */
        inline auto for_dirty() noexcept -> result<bool>;
        /**
         * Iterates over all pages and calls fn for each dirty page with a live handle to said page.
         *
         * \param fn dirty page handler; signature: result<void> fn(handle)
         * \returns a boolean indicating that fn has been called at least once or an error returned by fn.
         */
        template <typename Fn>
        inline auto for_dirty(Fn &&fn) noexcept -> result<bool>;

        /**
         * [DANGER] can deadlock if used concurrently with access()
         */
        inline void purge_all() noexcept;

        inline bool try_purge(handle &whom) noexcept;
        template <typename DisposeFn>
        inline bool try_purge(const key_type &whom, DisposeFn &&dispose) noexcept;

    private:
        inline auto try_purge(const key_type &key, history_list &history) noexcept
            -> std::optional<key_type>;
        [[nodiscard]] inline auto try_await_init(const key_type &key,
                                                 std::unique_lock<std::mutex> initGuard) -> handle;
        auto acquire_page(const key_type &key) noexcept -> page_index;
        auto replace() noexcept -> page_index;

        key_index_map mKeyIndexMap;
        page_allocator_type mPageAllocator;
        page_owner_type mPageOwner;  
        notify_dirty_fn mNotifyDirty;
                                      
        std::mutex mReplacementSync;

        clock mRecencyClock;
        clock mFrequencyClock;
        history_list mRecencyHistory;
        history_list mFrequencyHistory;
        index_key_map mIndexKeyMap;
                                        
        std::mutex mInitalizationSync;
        std::condition_variable mInitializationNotifier;
    };

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline auto cache_car<Key, T, CacheSize, Hash, KeyEqual>::pages() noexcept
        -> span<page_type, max_entries>
    {
        return {mPageOwner.get(), max_entries};
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline auto cache_car<Key, T, CacheSize, Hash, KeyEqual>::page(std::size_t id) noexcept
        -> page_type &
    {
        return mPageOwner.get()[id];
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline cache_car<Key, T, CacheSize, Hash, KeyEqual>::cache_car(notify_dirty_fn fn)
        : mKeyIndexMap(derive_key_index_map_size(max_entries))
        , mPageAllocator()
        , mPageOwner(allocate(mPageAllocator))
        , mNotifyDirty(std::move(fn))
        , mReplacementSync()
        , mRecencyClock()
        , mFrequencyClock()
        , mRecencyHistory()
        , mFrequencyHistory()
        , mIndexKeyMap()
        , mInitalizationSync()
        , mInitializationNotifier()
    {
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline cache_car<Key, T, CacheSize, Hash, KeyEqual>::~cache_car()
    {
        purge_all();
        mPageOwner.reset();
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline void cache_car<Key, T, CacheSize, Hash, KeyEqual>::purge_all() noexcept
    {
        std::lock_guard replacementGuard{mReplacementSync};
        mRecencyClock.clear();
        mFrequencyClock.clear();
        mRecencyHistory.clear();
        mFrequencyHistory.clear();

        bool finished;
        const auto pageSpan = pages();
        do
        {
            finished = true;
            for (auto &p : pageSpan)
            {
            deny_second_chance:
                enum_bitset<cache_replacement_result> rx = p.try_start_replace();
                if (rx == cache_replacement_result::succeeded)
                {
                    p.cancel_replace();
                }
                else if (rx == cache_replacement_result::second_chance)
                {
                    // not referenced, not dirty, _only_ second chance bit
                    goto deny_second_chance;
                }
                else
                {
                    finished = false;
                    if (rx % cache_replacement_result::dirty)
                    {
                        if (auto h = p.try_peek())
                        {
                            mNotifyDirty(std::move(h));
                        }
                    }
                }
            }
        } while (!finished);
        mKeyIndexMap.clear();
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline bool cache_car<Key, T, CacheSize, Hash, KeyEqual>::try_purge(handle &whom) noexcept
    {
        std::lock_guard guard{mReplacementSync};
        auto xpages = pages();
        auto  where = get_cache_index(whom, xpages.data());

        if (!xpages[where].try_purge(true))
        {
            return false;
        }
        whom = nullptr;
        if (!mRecencyClock.purge(where))
        {
            mFrequencyClock.purge(where);
        }
        mKeyIndexMap.erase_fn(std::move(mIndexKeyMap[where]),
                              [](auto stored) { return (stored & invalid_page_index_bit) == 0; });

        return true;
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    template <typename DisposeFn>
    inline bool
    cache_car<Key, T, CacheSize, Hash, KeyEqual>::try_purge(const key_type &whom,
                                                            DisposeFn &&dispose) noexcept
    {
        {
            page_index idx;
            std::lock_guard replacmentGuard{mReplacementSync};
            auto alive = !mKeyIndexMap.uprase_fn(
                whom,
                [this, &idx](auto &stored) {
                    if (stored & invalid_page_index_bit || page(stored).try_purge(false))
                    {
                        // someone does stuff therefore we must not change the stored index
                        idx = invalid_page_index_bit;
                    }
                    else
                    {
                        idx = std::exchange(stored, invalid_page_index_bit);
                    }
                    return false;
                },
                invalid_page_index_bit);

            if (alive && idx == invalid_page_index_bit)
            {
                return false;
            }

            if (alive)
            {
                if (!mRecencyClock.purge(idx))
                {
                    mFrequencyClock.purge(idx);
                }
                mIndexKeyMap[idx] = key_type{};
            }
            else
            {
                if (!try_purge(whom, mRecencyHistory))
                {
                    try_purge(whom, mFrequencyHistory);
                }
            }
        }
        std::invoke(dispose);

        // release access and inform anyone who waited
        mKeyIndexMap.erase_fn(whom, [this](auto stored) {
            if (stored != invalid_page_index_bit)
            {
                mInitializationNotifier.notify_all();
            }
            return true;
        });

        return true;
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline auto cache_car<Key, T, CacheSize, Hash, KeyEqual>::try_purge(
        const key_type &key, history_list &history) noexcept -> std::optional<key_type>
    {
        std::optional<key_type> recycled{std::nullopt};
        if (const auto it = std::find(history.begin(), history.end(), key); it != history.end())
        {
            recycled.emplace(std::move(*it));
            history.erase(it);
        }
        return recycled;
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline auto cache_car<Key, T, CacheSize, Hash, KeyEqual>::try_await_init(
        const key_type &key, std::unique_lock<std::mutex> initGuard) -> handle
    {
        const auto p = pages();
        handle h = {};

        // await initialization
        while (!h && initGuard.owns_lock())
        {
            mInitializationNotifier.wait(initGuard);
            // we need to unlock here in order to guarantee
            // a strict lock acquisition order
            // and that the owns_lock condition doesn't hold anymore if the key got erased
            initGuard.unlock();

            mKeyIndexMap.find_fn(key, [&](const page_index &stored) {
                if (stored & invalid_page_index_bit)
                {
                    initGuard.lock();
                }
                else
                {
                    // hit
                    h = p[stored].try_acquire();
                }
            });
        }

        return h;
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline auto cache_car<Key, T, CacheSize, Hash, KeyEqual>::try_access(const key_type &key)
        -> handle
    {
        handle h = {};
        std::unique_lock initGuard{mInitalizationSync, std::defer_lock};

        bool isInitializing = false;
        mKeyIndexMap.find_fn(key, [&](const page_index &stored) {
            isInitializing = stored & invalid_page_index_bit;
            if (!isInitializing)
            {
                // hit
                h = page(stored).try_acquire();
            }
        });

        mKeyIndexMap.update_fn(key, [&](page_index &stored) {
            if (stored & invalid_page_index_bit)
            {
                stored += 1; // make the initializer aware that we are waiting
                initGuard.lock();
            }
            else
            {
                // hit
                h = page(stored).try_acquire();
            }
        });

        if (isInitializing)
        {
            h = try_await_init(key, std::move(initGuard));
        }
        return h;
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    template <typename Ctor>
    inline auto cache_car<Key, T, CacheSize, Hash, KeyEqual>::access(const key_type &key,
                                                                     Ctor &&ctor) noexcept ->
        typename std::invoke_result_t<Ctor, void *>::template rebind<handle>
    {
        bool ignored;
        return access(std::move(key), std::forward<Ctor>(ctor), ignored);
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    template <typename Ctor>
    inline auto cache_car<Key, T, CacheSize, Hash, KeyEqual>::access(const key_type &key, Ctor &&ctor,
                                                                     bool &inserted) noexcept ->
        typename std::invoke_result_t<Ctor, void *>::template rebind<handle>
    {
        static_assert(std::is_nothrow_invocable_v<Ctor, void *>,
                      "the element constructor must be invocable with a void * memory location and "
                      "must also be noexcept");
        handle h = {};

        // retry loop
        // if the page exists we just acquire it, otherwise
        // race to acquire the initialization slot by inserting an invalid_page_index
        // if we loose the race we will offload to try_acquire again
        for (inserted = false; !inserted;)
        {
            std::unique_lock initGuard{mInitalizationSync, std::defer_lock};

            inserted = mKeyIndexMap.uprase_fn(key,
                                              [&](page_index &stored) {
                                                  if (!(stored & invalid_page_index_bit))
                                                  {
                                                      h = page(stored).try_acquire();
                                                  }
                                                  else
                                                  {
                                                      stored += 1;
                                                      initGuard.lock();
                                                  }
                                                  return false;
                                              },
                                              invalid_page_index_bit);

            if (initGuard.owns_lock())
            {
                h = try_await_init(key, std::move(initGuard));
            }
            if (h)
            {
                return h;
            }
        }

        page_index candidate = acquire_page(key);

        auto rx = page(candidate).finish_replace(std::forward<Ctor>(ctor));
        if constexpr (can_result_contain_failure_v<decltype(rx)>)
        {
            if (rx.has_failure())
            {
                // we only need to erase the key index map lookup
                // the remaining cleanup will happen automatically on
                // the next clock replacement cycle

                std::unique_lock initGuard{mInitalizationSync, std::defer_lock};
                mKeyIndexMap.erase_fn(key, [&](const page_index &stored) {
                    if (stored != invalid_page_index_bit)
                    {
                        initGuard.lock();
                    }
                    return true;
                });

                if (initGuard.owns_lock())
                {
                    initGuard.unlock();
                    mInitializationNotifier.notify_all();
                }
                return std::move(rx).as_failure();
            }
        }
        h = std::move(rx).assume_value();

        {
            std::unique_lock initGuard{mInitalizationSync, std::defer_lock};
            mKeyIndexMap.update_fn(key, [&](page_index &stored) {
                if (stored != invalid_page_index_bit)
                {
                    // someone is awaiting initilization
                    initGuard.lock();
                }
                stored = candidate;
            });
            if (initGuard.owns_lock())
            {
                initGuard.unlock();
                mInitializationNotifier.notify_all();
            }
        }

        return h;
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    template <typename... Args>
    inline auto cache_car<Key, T, CacheSize, Hash, KeyEqual>::access(const key_type &key,
                                                                     Args &&... ctorArgs) noexcept
        -> std::conditional_t<std::is_nothrow_constructible_v<value_type, Args...>, handle,
                              op_outcome<handle>>
    {
        if constexpr (std::is_nothrow_constructible_v<value_type, Args...>)
        {
            return access(key,
                          [&](void *p) noexcept->result<value_type *, void> {
                              return new (p) value_type(std::forward<Args>(ctorArgs)...);
                          })
                .assume_value();
        }
        else
        {
            return access(key, [&](void *p) noexcept->op_outcome<value_type *> {
                try
                {
                    return new (p) value_type(std::forward<Args>(ctorArgs)...);
                }
                catch (const std::bad_alloc &)
                {
                    return failure(errc::not_enough_memory);
                }
                catch (...)
                {
                    return failure(std::current_exception());
                }
            });
        }
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline auto cache_car<Key, T, CacheSize, Hash, KeyEqual>::for_dirty() noexcept -> result<bool>
    {
        return for_dirty([this](handle h) noexcept->result<void> {
            mNotifyDirty(std::move(h));
            return success();
        });
    }
    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    template <typename Fn>
    inline auto cache_car<Key, T, CacheSize, Hash, KeyEqual>::for_dirty(Fn &&fn) noexcept
        -> result<bool>
    {
        static_assert(std::is_nothrow_invocable_v<Fn, handle>);

        bool anyDirty = false;
        for (auto &page : pages())
        {
            if (auto h = page.try_peek(); h && h.is_dirty())
            {
                anyDirty = true;
                VEFS_TRY(std::invoke(fn, std::move(h)));
            }
        }
        return anyDirty;
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline auto
    cache_car<Key, T, CacheSize, Hash, KeyEqual>::acquire_page(const key_type &key) noexcept
        -> page_index
    {
        page_index candidate = {};
        std::lock_guard guard{mReplacementSync};

        auto recycled = try_purge(key, mRecencyHistory);
        const bool hasRecencyEntry{recycled};
        if (!hasRecencyEntry)
        {
            recycled = try_purge(key, mFrequencyHistory);
        }
        const bool hasFrequencyEntry = !hasRecencyEntry && recycled;
        const bool isNew = !hasRecencyEntry && !hasFrequencyEntry;

        if (const auto num_entries = mRecencyClock.size() + mFrequencyClock.size();
            num_entries == max_entries)
        {
            candidate = replace();

            if (isNew)
            {
                if (mRecencyClock.size() + mRecencyHistory.size() == max_entries)
                {
                    mRecencyHistory.pop_back();
                }
                else if (num_entries + mRecencyHistory.size() + mFrequencyHistory.size() ==
                         2 * max_entries)
                {
                    mFrequencyHistory.pop_back();
                }
            }
        }
        else
        {
            // not full, candidate is _usually_ at the end of the valid cache page range
            // this is however not necessarily the case after purging a page
            candidate = num_entries;
            if (!page(candidate).is_dead())
            {
                auto xpages = pages();
                auto which =
                    std::find_if(xpages.begin(), xpages.end(), [](auto &p) { return p.is_dead(); });
                candidate = static_cast<page_index>(std::distance(xpages.begin(), which));
            }
            [[maybe_unused]] const auto rprx = page(candidate).try_start_replace();
            assert(rprx == cache_replacement_result::succeeded);
        }

        if (isNew)
        {
            mRecencyClock.push_back(candidate);
        }
        else
        {
            // we need a signed type, because the !hasRecency part might underflow
            using signed_type = typename boost::int_max_value_t<max_entries * 2>::fast;
            const auto recencySize = static_cast<signed_type>(mRecencyHistory.size());
            const auto frequencySize = static_cast<signed_type>(mFrequencyHistory.size());
            const auto currentTarget = static_cast<signed_type>(mRecencyClock.size_target());
            const auto sizeTarget =
                hasRecencyEntry
                    ? std::min<signed_type>(
                          currentTarget +
                              std::max<signed_type>(1, frequencySize / (recencySize + 1)),
                          max_entries)
                    : std::max<signed_type>(
                          currentTarget -
                              std::max<signed_type>(1, recencySize / (frequencySize + 1)),
                          0);

            mRecencyClock.size_target(static_cast<std::size_t>(sizeTarget));
            mFrequencyClock.push_back(candidate);
        }
        mIndexKeyMap[candidate] = std::move(recycled).value_or(key);

        return candidate;
    }

    template <typename Key, typename T, unsigned int CacheSize, typename Hash, typename KeyEqual>
    inline auto cache_car<Key, T, CacheSize, Hash, KeyEqual>::replace() noexcept -> page_index
    {
        auto p = pages();
        page_index candidate = {};
        for (;;)
        {
            if (mRecencyClock.size() >= std::max<std::size_t>(1, mRecencyClock.size_target()))
            {
                candidate = mRecencyClock.pop_front();
                if (auto rx = p[candidate].try_start_replace(); rx == cache_replacement_result::succeeded)
                {
                    // evicted -> move to recency history
                    mKeyIndexMap.erase(mIndexKeyMap[candidate]);
                    mRecencyHistory.insert(mRecencyHistory.begin(),
                                           std::move(mIndexKeyMap[candidate]));
                    break;
                }
                else
                {
                    (rx % cache_replacement_result::second_chance
                         ? mFrequencyClock // has been accessed more than once
                                           // -> goto frequency list
                         : mRecencyClock)  // can't evict
                                           // -> ask for cleanup and back into the recency list.
                        .push_back(candidate);

                    // if dirty the owner may want to clean it
                    if (rx % cache_replacement_result::dirty && mNotifyDirty)
                    {
                        mNotifyDirty(p[candidate].try_peek());
                    }
                }
            }
            else
            {
                candidate = mFrequencyClock.pop_front();
                if (auto rx = p[candidate].try_start_replace(); rx == cache_replacement_result::succeeded)
                {
                    // evicted -> move to frequency history
                    mKeyIndexMap.erase(mIndexKeyMap[candidate]);
                    mFrequencyHistory.insert(mFrequencyHistory.begin(),
                                             std::move(mIndexKeyMap[candidate]));
                    break;
                }
                else
                {
                    mFrequencyClock.push_back(candidate);

                    // if dirty the owner may want to clean it
                    if (rx % cache_replacement_result::dirty && mNotifyDirty)
                    {
                        mNotifyDirty(p[candidate].try_peek());
                    }
                }
            }
        }

        return candidate;
    }

    template class cache_car<int, int, 64>;
} // namespace vefs::detail
