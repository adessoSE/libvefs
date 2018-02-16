#pragma once

#include <array>
#include <mutex>
#include <memory>
#include <atomic>
#include <optional>
#include <functional>
#include <type_traits>
#include <condition_variable>

#include <vefs/ext/libcuckoo/cuckoohash_map.hh>

#include <vefs/utils/allocator.hpp>
#include <vefs/utils/pool_allocator.hpp>

namespace vefs::detail::detail
{
    template <typename T>
    struct cache_entry;

    template <typename T>
    class [[nodiscard]] cache_handle
    {
        using entry_type = cache_entry<T>;
        friend struct entry_type;

    public:
        using element_type = std::remove_extent_t<T>;

        constexpr cache_handle() noexcept
            : mData{ nullptr }
            , mControl{ nullptr }
        {
        }
        inline cache_handle(const cache_handle &other)
            : mData{ other.mData }
            , mControl{ other.mControl }
        {
            mControl->add_reference();
        }
        constexpr cache_handle(cache_handle &&other) noexcept
            : mData{ other.mData }
            , mControl{ other.mControl }
        {
            other.mData = nullptr;
            other.mControl = nullptr;
        }
        ~cache_handle()
        {
            if (*this)
            {
                mControl->release();
            }
        }

        inline cache_handle & operator=(const cache_handle &other)
        {
            if (*this)
            {
                mControl->release();
            }
            mData = other.mData;
            mControl = other.mControl;
            mControl->add_reference();
            return *this;
        }
        inline cache_handle & operator=(cache_handle &&other) noexcept
        {
            if (*this)
            {
                mControl->release();
            }
            mData = other.mData;
            other.mData = nullptr;
            mControl = other.mControl;
            other.mControl = nullptr;
            return *this;
        }
        inline cache_handle & operator=(std::nullptr_t) noexcept
        {
            if (*this)
            {
                mControl->release();
            }
            mData = nullptr;
            mControl = nullptr;
            return *this;
        }

        explicit operator bool() const noexcept
        {
            return mControl;
        }

        inline element_type * get() const noexcept
        {
            return mData;
        }

        inline element_type & operator*() const noexcept
        {
            return *mData;
        }
        inline element_type * operator->() const noexcept
        {
            return mData;
        }

        template <typename Y>
        inline bool owner_before(const cache_handle<Y> other) const noexcept
        {
            return mControl < other.mControl;
        }

        inline void mark_dirty() const noexcept
        {
            mControl->mark_dirty();
        }
        inline void mark_clean() const noexcept
        {
            mControl->mark_clean();
        }
        inline bool is_dirty() const noexcept
        {
            return mControl->is_dirty();
        }

        friend inline void swap(cache_handle &lhs, cache_handle &rhs) noexcept
        {
            using std::swap;
            swap(lhs.mData, rhs.mData);
            swap(lhs.mControl, rhs.mControl);
        }

    private:
        inline cache_handle(cache_entry<T> &entry) noexcept;

        element_type *mData;
        entry_type *mControl;
    };

    enum class replacement_result
    {
        dirty = -1,
        failed = 0,
        was_dead = 1,
        was_alive = 2,
    };

    template <typename T>
    struct cache_entry
    {
        using state_type = std::size_t;
        using state_traits = std::numeric_limits<state_type>;

        static constexpr state_type one = 1;

        static constexpr state_type TombstoneBit = one << (state_traits::digits - 1);
        static constexpr state_type DirtyBit = TombstoneBit >> 1;
        static constexpr state_type DirtyTombstone = TombstoneBit | DirtyBit;
        static constexpr state_type SecondChanceBit = DirtyBit >> 1;

        constexpr cache_entry()
            : mEntryState(TombstoneBit)
            , mValuePtr(nullptr)
        {
        }

        inline bool is_dead() const
        {
            return mEntryState.load(std::memory_order_acquire) & TombstoneBit;
        }
        inline replacement_result try_start_replace()
        {
            if (mEntryState.fetch_and(~SecondChanceBit, std::memory_order_acq_rel)
                & SecondChanceBit)
            {
                // respect second chance
                return replacement_result::failed;
            }

            state_type current = mEntryState.load(std::memory_order_acquire);
            do
            {
                // we only allow replacement if this state is zero or
                // it is a non dirty tombstone
                if (current != 0 && (current & DirtyTombstone) != TombstoneBit)
                {
                    return replacement_result::failed;
                }

            } while (!mEntryState.compare_exchange_weak(current, DirtyTombstone,
                        std::memory_order_acq_rel, std::memory_order_acquire));

            return current & TombstoneBit
                ? replacement_result::was_dead
                : replacement_result::was_alive;
        }
        inline cache_handle<T> finish_replace(bool success)
        {
            const auto val = success ? one : TombstoneBit;
            mEntryState.store(val, std::memory_order_release);
            return success ? cache_handle<T>{ *this } : cache_handle<T>{};
        }
        inline cache_handle<T> try_acquire()
        {
            if (try_add_reference())
            {
                mEntryState.fetch_or(SecondChanceBit, std::memory_order_release);
                return { *this };
            }
            return {};
        }
        inline cache_handle<T> try_peek()
        {
            if (try_add_reference())
            {
                return { *this };
            }
            return {};
        }

        inline bool is_dirty() const
        {
            return mEntryState.load(std::memory_order_acquire) & DirtyBit;
        }
        inline void mark_dirty()
        {
            mEntryState.fetch_or(DirtyBit, std::memory_order_release);
        }
        inline void mark_clean()
        {
            mEntryState.fetch_and(~DirtyBit, std::memory_order_release);
        }
        inline void add_reference()
        {
            mEntryState.fetch_add(1, std::memory_order_release);
        }
        inline void release()
        {
            mEntryState.fetch_sub(1, std::memory_order_release);
        }

    private:
        inline bool try_add_reference()
        {
            return !(mEntryState.fetch_add(1, std::memory_order_acq_rel) & TombstoneBit);
        }

    public:

        std::atomic<std::size_t> mEntryState{ std::numeric_limits<std::size_t>::max() };
        std::atomic<T *> mValuePtr{ nullptr };
    };

    template <typename T>
    inline cache_handle<T>::cache_handle(cache_entry<T> &entry) noexcept
        : mData{ entry.mValuePtr.load(std::memory_order_acquire) }
        , mControl{ &entry }
    {
    }

    enum class cache_lookup_state
    {
        alive,
        initializing,
        failed,
    };

    struct cache_lookup
    {
        using entry_index = std::size_t;

        static constexpr entry_index invalid = std::numeric_limits<entry_index>::max();

        std::mutex sync;
        entry_index index;
        cache_lookup_state state;
        std::condition_variable ready_condition;

        inline cache_lookup()
            : sync{}
            , ready_condition{}
            , index{ invalid }
            , state{ cache_lookup_state::initializing }
        {
        }
    };

    using cache_lookup_ptr = std::shared_ptr<cache_lookup>;

    template <std::size_t num_entries>
    using cache_lookup_pool_allocator = utils::alloc_std_adaptor<cache_lookup,
        utils::octopus_allocator<
            utils::pool_allocator_mt<
                sizeof(cache_lookup) + 2 * sizeof(std::size_t), num_entries,
                utils::default_system_allocator
            >,
            utils::default_system_allocator
        >
    >;

    using temp_alloc_t = cache_lookup_pool_allocator<2014>;

    inline cache_lookup_ptr create_lookup(temp_alloc_t &allocator)
    {
        return std::allocate_shared<cache_lookup>(allocator);
    }
}

namespace std
{
    template <typename T>
    struct owner_less<vefs::detail::detail::cache_handle<T>>
    {
    private:
        using cache_handle = vefs::detail::detail::cache_handle<T>;

    public:
        constexpr bool operator()(const cache_handle &lhs, const cache_handle &rhs)
        {
            return lhs.owner_before(rhs);
        }
    };
}

namespace vefs::detail
{
    template <typename Key, typename T>
    using cache_default_map = cuckoohash_map<Key, T, std::hash<Key>, std::equal_to<>>;

    template <typename Key, typename T, std::size_t cacheLimit,
        typename InstanceAllocator = utils::system_allocator<alignof(T)>,
        template<typename, typename> typename CacheMap = cache_default_map
    >
    class cache
    {
        using entry_index = std::size_t;
        static constexpr std::size_t max_entries = cacheLimit;
        using allocator_t = InstanceAllocator;
        static constexpr std::size_t instance_alloc_size = sizeof(T);

    public:
        using key_type = Key;
        using value_type = T;

    private:
        using entry_type = detail::cache_entry<value_type>;

        using lookup = detail::cache_lookup;
        using lookup_state = detail::cache_lookup_state;
        using lookup_ptr = detail::cache_lookup_ptr;

        /*
        using lookup_allocator_t = utils::alloc_std_adaptor<lookup,
            utils::octopus_allocator<
                utils::pool_allocator_mt<
                    sizeof(lookup) + 2*sizeof(std::size_t), max_entries + 16,
                    utils::default_system_allocator
                >,
                utils::default_system_allocator
            >
        >;
        */
        using lookup_allocator_t = detail::temp_alloc_t;

        using key_index_map = CacheMap<key_type, lookup_ptr>;
        using index_key_map = std::array<key_type, max_entries>;

        using cache_table = std::array<entry_type, max_entries>;

        using ring_counter = utils::detail::atomic_ring_counter<max_entries>;

        inline lookup_ptr create_lookup()
        {
            //return std::allocate_shared<lookup>(mLookupAllocator);
            return detail::create_lookup(mLookupAllocator);
        }

    public:
        using handle = detail::cache_handle<value_type>;
        using notify_dirty_fn = std::function<void(handle)>;

        enum class preinit_storage_t {};
        static constexpr preinit_storage_t preinit_storage = preinit_storage_t{};

        inline cache(notify_dirty_fn fn)
            : mClockHand{ 0 }
            , mNotifyDirty{ std::move(fn) }
            , mKeyIndexMap{}
            , mEntries{}
            , mIndexKeyMap{}
            , mLookupAllocator{}
            , mInstanceAllocator{}
        {
        }
        inline cache(notify_dirty_fn fn, preinit_storage_t)
            : cache{ std::move(fn) }
        {
            try
            {
                for (auto &e : mEntries)
                {
                    utils::maybe_allocation alloc
                        = mInstanceAllocator.allocate(instance_alloc_size);

                    if (!alloc)
                    {
                        BOOST_THROW_EXCEPTION(std::bad_alloc{});
                    }

                    e.mValuePtr.store(reinterpret_cast<T*>(alloc->raw()),
                        std::memory_order_release);
                }
            }
            catch (...)
            {
                for (auto &e : mEntries)
                {
                    if (auto ptr = e.mValuePtr.load(std::memory_order_acquire))
                    {
                        auto bptr = reinterpret_cast<std::byte *>(ptr);
                        mInstanceAllocator.deallocate({ bptr, instance_alloc_size });
                        e.mValuePtr.store(nullptr, std::memory_order_release);
                    }
                }
            }
        }
        inline ~cache()
        {
            for (auto &e : mEntries)
            {
                assert(!e.is_dirty());
                if (auto ptr = e.mValuePtr.load(std::memory_order_acquire))
                {
                    auto bptr = reinterpret_cast<std::byte *>(ptr);
                    mInstanceAllocator.deallocate({ bptr, instance_alloc_size });
                    e.mValuePtr.store(nullptr, std::memory_order_release);
                }
            }
        }

        inline handle try_access(const key_type &key)
        {
            lookup_ptr lptr;
            mKeyIndexMap.find_fn(key, [&lptr](const lookup_ptr &l)
            {
                lptr = l;
            });

            if (lptr)
            {
                std::unique_lock<std::mutex> lock{ lptr->sync };
                switch (lptr->state)
                {
                case lookup_state::initializing:
                    lptr->ready_condition.wait(lock, [&lptr]()
                    {
                        return lptr->state == lookup_state::initializing;
                    });
                    if (lptr->state != lookup_state::alive)
                    {
                        break;
                    }

                case lookup_state::alive:
                    return mEntries[lptr->index].try_acquire();

                default:
                    break;
                }
            }
            return {};
        }

        template <typename... ConstructorArgs>
        [[nodiscard]]
        inline std::tuple<bool, handle> access(const key_type &key,
            ConstructorArgs&&... ctorArgs)
        {
            bool inserted = false;
            lookup_ptr lptr;

            // we first try to find the cached value in order to avoid any
            // memory allocations when we have a cache hit.
            if (!mKeyIndexMap.find_fn(key, [&lptr](const lookup_ptr &l) { lptr = l; }))
            {
                lptr = create_lookup();
                inserted = mKeyIndexMap.uprase_fn(key, [&lptr](const lookup_ptr &l)
                {
                    lptr = l;
                    return false;
                }, lptr);
            }

            // if we inserted the lookup element, we are already responsible for
            // the initialization, otherwise we need to inspect the lookup state
            if (!inserted)
            {
                std::unique_lock<std::mutex> guard{ lptr->sync };

                while (lptr->state == lookup_state::initializing)
                {
                    lptr->ready_condition.wait(guard);
                }

                if (lptr->state == lookup_state::alive)
                {
                    if (auto valueHandle = mEntries[lptr->index].try_acquire())
                    {
                        return { true, std::move(valueHandle) };
                    }
                }

                lptr->state = lookup_state::initializing;
            }

            // if we fail, we inform someone about it
            VEFS_ERROR_EXIT{
                {
                    std::lock_guard<std::mutex> guard{ lptr->sync };
                    lptr->index = lookup::invalid;
                    lptr->state = lookup_state::failed;
                }
                lptr->ready_condition.notify_one();
            };

            // it's our turn to insert the element
            auto[index, modus] = aquire_tile();
            auto &entry = mEntries[index];

            VEFS_ERROR_EXIT{
                // discard the empty handle while avoiding compiler warnings
                [[maybe_unused]] auto _ = entry.finish_replace(false);
            };

            void *memptr;
            if (auto cptr = entry.mValuePtr.load(std::memory_order_acquire))
            {
                // memory is already allocated and will be reused
                if constexpr (!std::is_trivially_destructible_v<T>)
                {
                    if (modus == detail::replacement_result::was_alive)
                    {
                        std::destroy_at(cptr);
                    }
                }
                else
                {
                    (void)modus;
                }
                memptr = cptr;
            }
            else
            {
                utils::maybe_allocation alloc
                    = mInstanceAllocator.allocate(instance_alloc_size);

                if (!alloc)
                {
                    BOOST_THROW_EXCEPTION(std::bad_alloc{});
                }

                // prevent the newly allocated memory from leaking in case of
                // a constructor exception
                memptr = alloc->raw();
                entry.mValuePtr.store(reinterpret_cast<T *>(memptr),
                    std::memory_order_release);
            }

            // might throw
            auto ptr = new(memptr) T(std::forward<ConstructorArgs>(ctorArgs)...);

            entry.mValuePtr.store(ptr, std::memory_order_release);
            mIndexKeyMap[index] = key;
            auto valueHandle = entry.finish_replace(true);

            {
                std::lock_guard<std::mutex> guard{ lptr->sync };
                lptr->state = lookup_state::alive;
                lptr->index = index;
            }
            lptr->ready_condition.notify_all();

            return { false, valueHandle };
        }

        template <typename Fn>
        inline bool for_dirty(Fn &&fn)
        {
            bool anyDirty = false;
            for (auto &e : mEntries)
            {
                if (auto h = e.try_peek(); e && e.is_dirty())
                {
                    anyDirty = true;
                    fn(std::move(h));
                }
            }
            return anyDirty;
        }

    private:
        inline std::tuple<entry_index, detail::replacement_result> aquire_tile()
        {
            for (auto i = 0; ; ++i)
            {
                auto next = mClockHand.fetch_next();
                auto &entry = mEntries[next];

                auto result = entry.try_start_replace();
                switch (result)
                {
                case detail::replacement_result::was_alive:
                    cleanup_tile(next);
                case detail::replacement_result::was_dead:
                    return { next, result };

                case detail::replacement_result::dirty:
                    if (mNotifyDirty)
                    {
                        if (auto h = entry.try_peek())
                        {
                            mNotifyDirty(h);
                        }
                    }

                case detail::replacement_result::failed:
                default:
                    break;
                }

                if (i % (max_entries * 2) == 0)
                {
                    std::this_thread::yield();
                }
            }
        }

        inline void cleanup_tile(entry_index tileIdx)
        {
            const auto &key = mIndexKeyMap[tileIdx];
            mKeyIndexMap.erase_fn(key, [this](lookup_ptr &oldptr)
            {
                {
                    std::lock_guard<std::mutex> guard{ oldptr->sync };
                    oldptr->state = lookup_state::failed;
                }
                oldptr->ready_condition.notify_all();

                std::weak_ptr<lookup> weakref{ oldptr };
                oldptr.reset();
                return !(oldptr = weakref.lock());
            });
        }

        ring_counter mClockHand;
        notify_dirty_fn mNotifyDirty;
        key_index_map mKeyIndexMap;
        cache_table mEntries;
        index_key_map mIndexKeyMap;
        lookup_allocator_t mLookupAllocator;
        allocator_t mInstanceAllocator;
    };
}
