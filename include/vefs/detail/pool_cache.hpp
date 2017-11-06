#pragma once

#include <cstddef>

#include <thread>
#include <utility>

#include <vefs/detail/cache.hpp>
#include <vefs/detail/memory_pool.hpp>

namespace vefs::detail
{
    template <typename Cache, std::size_t NumChunks>
    class caching_object_pool
    {
        using cache_t = Cache;
        using pool_t = shared_object_pool<typename cache_t::value_type, NumChunks>;

    public:
        using key_type = typename cache_t::key_type;
        using value_type = typename cache_t::value_type;
        using handle = typename cache_t::handle;

        using is_dirty_fn = typename cache_t::is_dirty_fn;

        caching_object_pool(is_dirty_fn isDirtyPredicate)
            : mCache(std::move(isDirtyPredicate))
        {
        }

        template <typename Fn, typename... ConstructorArgs>
        std::tuple<bool, handle> access(const key_type &key, Fn &&initFn, ConstructorArgs... ctorArgs)
        {
            return try_access<std::numeric_limits<std::size_t>::max()>(key, std::forward<Fn>(initFn),
                std::move(ctorArgs)...);
        }

        handle try_access(const key_type &key)
        {
            return mCache.try_access(key);
        }

        template <std::size_t maxTries, typename Fn, typename... ConstructorArgs>
        std::tuple<bool, handle> try_access(const key_type &key, Fn &&initFn, ConstructorArgs... ctorArgs)
        {
            if (auto cache_ptr = mCache.try_access(key))
            {
                return { true, cache_ptr };
            }
            else
            {
                auto elem = mStorage.create(ctorArgs...);
                [[maybe_unused]] auto tries = maxTries;
                while (!elem)
                {
                    std::this_thread::yield();
                    mCache.make_room();
                    elem = mStorage.create(ctorArgs...);

                    if constexpr (maxTries)
                    {
                        if (!--tries)
                        {
                            return { false, {} };
                        }
                    }
                }
                initFn(*elem);
                return mCache.try_push(key, std::move(elem));
            }
        }

        std::tuple<bool, handle> try_push_external(key_type key, handle obj,
            typename cache_t::on_free_fn freeFn)
        {
            return mCache.try_push(std::move(key), std::move(obj), std::move(freeFn));
        }
        void purge(const key_type &key, const handle &obj)
        {
            mCache.purge(key, obj);
        }

    private:
        cache_t mCache;
        pool_t mStorage;
    };
}
