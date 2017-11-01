#pragma once

#include <cstddef>

#include <utility>

#include <vefs/detail/cache.hpp>
#include <vefs/detail/memory_pool.hpp>

namespace vefs::detail
{
    template <typename T, std::size_t NumChunks>
    class caching_object_pool
    {
        using cache_t = cache<T>;
        using pool_t = shared_object_pool<T, NumChunks>;

    public:
        using handle = typename cache_t::handle;

        template <typename Fn, typename... ConstructorArgs>
        std::tuple<bool, handle> access(const detail::shared_string &blockId, Fn &&initFn, ConstructorArgs... ctorArgs)
        {
            if (auto cache_ptr = mCache.try_access(blockId))
            {
                return { true, cache_ptr };
            }
            else
            {
                auto block = mStorage.create(ctorArgs...);
                while (!block)
                {
                    mCache.make_room();
                    block = mStorage.create(ctorArgs...);
                }
                initFn(*block);
                return mCache.try_push(blockId, std::move(block));
            }
        }

    private:
        cache_t mCache;
        pool_t mStorage;
    };
}
