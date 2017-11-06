#pragma once

#include <cstddef>
#include <cassert>

#include <memory>
#include <thread>
#include <utility>
#include <typeinfo>
#include <functional>
#include <type_traits>

#include <vefs/ext/libcuckoo/cuckoohash_map.hh>
#include <vefs/ext/concurrentqueue/concurrentqueue.h>

#include <vefs/detail/memory_pool.hpp>

namespace vefs::detail
{
    template <typename Key, typename T, std::size_t NumChunks>
    class caching_object_pool
    {
        static constexpr std::size_t alignment_mismatch = sizeof(T) % alignof(T);
        static constexpr std::size_t block_size
            = sizeof(T) + !!alignment_mismatch * (alignof(T) - alignment_mismatch);

    public:
        using key_type = Key;
        using value_type = T;
        using handle = std::shared_ptr<value_type>;

        using is_dirty_fn = std::function<bool(const key_type &, handle)>;
        using on_free_fn = std::function<void(const key_type &, value_type *)>;

    private:
        enum class node_type
        {
            simple = 0,
            external = 1,
        };

        struct node
        {
            node(caching_object_pool *owner, key_type id)
                : node(node_type::simple, owner, std::move(id))
            {
            }
            node(const node &) = delete;
            ~node()
            {
                if (owner)
                {
                    owner->destroy(cached_object);
                    owner->on_node_destruction(id);
                }
            }

            node & operator=(const node &) = delete;

            void zombify()
            {
                owner = nullptr;
            }

            const node_type type;
            caching_object_pool *owner;
            value_type *cached_object;
            key_type id;

        protected:
            node(node_type type, caching_object_pool *owner, key_type id)
                : type(type)
                , owner(owner)
                , id(std::move(id))
                , cached_object(nullptr)
            {
            }
        };
        struct extern_node
            : node
        {
            extern_node(caching_object_pool *owner, key_type id, on_free_fn onFree)
                : node(node_type::external, owner, std::move(id))
                , on_free(std::move(onFree))
            {
            }
            ~extern_node()
            {
                if (this->cached_object)
                {
                    on_free(this->id, this->cached_object);
                    this->cached_object = nullptr;
                }
            }

            on_free_fn on_free;
        };

        using node_ptr = std::shared_ptr<node>;
        using node_weak_ptr = std::weak_ptr<node>;

        using cache_map = cuckoohash_map<key_type, node_weak_ptr>;
        using access_queue = moodycamel::ConcurrentQueue<node_ptr>;
        using object_pool = block_memory_pool<block_size, NumChunks, alignof(T)>;

    public:
        caching_object_pool(is_dirty_fn isDirtyFn)
            : is_dirty(std::move(isDirtyFn))
        {
        }
        ~caching_object_pool()
        {
            auto lockedCache = mCachedValues.lock_table();
            for (auto &[id, weak_nptr] : lockedCache)
            {
                (void)id;
                if (auto nptr = weak_nptr.lock())
                {
                    dispose(std::move(nptr));
                }
            }
            lockedCache.clear();
        }

        handle try_access(const key_type &key)
        {
            node_ptr n;
            mCachedValues.find_fn(key, [&n](const node_weak_ptr &weak_node)
            {
                n = weak_node.lock();
            });
            if (n)
            {
                // external objects are never pushed into the access queue
                // in order to release them as soon as possible
                if (n->type != node_type::external)
                {
                    mAccessQueue.enqueue(std::move(n));
                }
                return as_handle(n);
            }
            return {};
        }

        template <std::size_t maxTries, typename Fn, typename... ConstructorArgs>
        std::tuple<bool, handle> try_access(const key_type &key, Fn &&initFn, ConstructorArgs... ctorArgs)
        {
            if (auto cache_ptr = this->try_access(key))
            {
                return { true, cache_ptr };
            }
            else
            {
                auto nptr = try_create(key, ctorArgs...);
                [[maybe_unused]] auto tries = maxTries;
                //TODO: passive wait
                while (!nptr)
                {
                    std::this_thread::yield();
                    make_room();
                    nptr = try_create(key, ctorArgs...);

                    if constexpr (maxTries)
                    {
                        if (!--tries)
                        {
                            return { false, {} };
                        }
                    }
                }
                initFn(*nptr->cached_object);
                return try_push<>(std::move(nptr));
            }
        }

        template <typename Fn, typename... ConstructorArgs>
        std::tuple<bool, handle> access(const key_type &key, Fn &&initFn, ConstructorArgs... ctorArgs)
        {
            return this->try_access<std::numeric_limits<std::size_t>::max()>(key, std::forward<Fn>(initFn),
                std::move(ctorArgs)...);
        }

        void make_room()
        {
            for (std::size_t i = 0, limit = mAccessQueue.size_approx(); i < limit; ++i)
            {
                node_ptr nptr;
                mAccessQueue.try_dequeue(nptr);
                if (nptr && is_dirty(nptr->id, as_handle(nptr)))
                {
                    mAccessQueue.enqueue(std::move(nptr));
                }
                else
                {
                    node_weak_ptr postMortem{ nptr };
                    nptr.reset();
                    // check wether the reset killed the kid
                    if (postMortem.expired())
                    {
                        break;
                    }
                }
            }
        }

        // transfers ownership of @param object to the cache; the given object reference must
        // not be used after this function got called.
        // Please note that the cache will only hold weak references to the external object
        // which means that discarding the returned handle might cause immediate destruction of the
        // given object
        // the method guarantees that object will be eventually and atomically passed to freeFn
        // which should delete the object.
        // the method returns false if and only if object
        [[nodiscard]] std::tuple<bool, handle> try_push_external(const key_type &key, value_type &object, on_free_fn freeFn)
        {
            std::shared_ptr<extern_node> ext_node;
            try
            {
                ext_node = std::make_shared<extern_node>(this, key, freeFn);
                ext_node->cached_object = &object;
            }
            catch (...)
            {
                freeFn(key, &object);
                throw;
            }

            return try_push<false>(std::static_pointer_cast<node>(ext_node));
        }

    private:
        handle as_handle(const node_ptr &node)
        {
            return { node, node->cached_object };
        }

        template <typename... Args>
        node_ptr try_create(key_type id, Args&&... args)
        {
            if (auto mem = mMemPool.try_alloc())
            {
                try
                {
                    auto n = std::make_shared<node>(this, std::move(id));

                    n->cached_object = new (mem.data()) T(std::forward<Args>(args)...);

                    return n;
                }
                catch (...)
                {
                    mMemPool.dealloc(mem);
                    throw;
                }

            }
            return {};
        }

        void destroy(value_type *cachedObj)
        {
            if (cachedObj)
            {
                cachedObj->~T();
                mMemPool.dealloc({ reinterpret_cast<std::byte *>(cachedObj), block_size });
            }
        }
        // gets rid of a node's content preventing the on_node_destruction callback
        // and without locking anything
        void dispose(node_ptr &&nptr)
        {
            switch (nptr->type)
            {
            case node_type::simple:
                destroy(nptr->cached_object);
                nptr->cached_object = nullptr;
                [[fallthrough]];

            default:
                nptr->zombify();
                break;
            };
        }

        template <bool register_access = true>
        std::tuple<bool, handle> try_push(node_ptr n)
        {
            bool existing = false;
            mCachedValues.uprase_fn(n->id, [&existing, &n](node_weak_ptr &mapNode)
            {
                if (auto existingNode = mapNode.lock())
                {
                    existing = true;

                    // kill the zombie orphan before it causes a dead lock *,..,*
                    auto owner = n->owner;
                    owner->dispose(std::move(n));

                    n = std::move(existingNode);
                }
                else
                {
                    mapNode = n;
                }
                return false;
            }, n);

            auto resource = as_handle(n);
            if constexpr (register_access)
            {
                mAccessQueue.enqueue(std::move(n));
            }
            return { existing, resource };
        }

        void on_node_destruction(key_type node_id)
        {
            mCachedValues.erase_fn(node_id, [](node_weak_ptr &n)
            {
                // if this is called by the node ptr destructor
                // n returns is required to return true except
                // someone pushed a new object already in which case
                // we don't want to erase it
                return n.expired();
            });
        }

        is_dirty_fn is_dirty;
        access_queue mAccessQueue;
        cache_map mCachedValues;
        object_pool mMemPool;
    };
}
