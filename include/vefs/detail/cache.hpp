#pragma once

#include <memory>
#include <functional>
#include <type_traits>

#include <vefs/ext/libcuckoo/cuckoohash_map.hh>
#include <vefs/ext/concurrentqueue/concurrentqueue.h>

namespace vefs::detail
{
    template <class Key, class T, class Hash = std::hash<Key>,
          class KeyEqual = std::equal_to<Key>,
          class Allocator = std::allocator<std::pair<const Key, T>>,
          std::size_t SLOT_PER_BUCKET = LIBCUCKOO_DEFAULT_SLOT_PER_BUCKET>
    class cache
    {
    public:
        using key_type = Key;
        using value_type = T;
        using handle = std::shared_ptr<value_type>;
        using weak_handle = typename handle::weak_type;

        using is_dirty_fn = std::function<bool(handle)>;
        using on_free_fn = std::function<void(key_type &, handle)>;

    private:
        struct node
        {
            node(cache *owner, key_type id, handle object)
                : owner(owner)
                , id(id)
                , cached_object(std::move(object))
            {
            }
            node(const node &) = delete;
            ~node()
            {
                if (owner)
                {
                    owner->on_node_destruction(id);
                }
            }

            node & operator=(const node &) = delete;

            void on_owner_released()
            {
                owner = nullptr;
            }

            cache *owner;
            handle cached_object;
            key_type id;
        };
        struct augmented_node
            : node
        {
            augmented_node(cache *owner, key_type id, handle object, on_free_fn onFree)
                : node(owner, std::move(id), std::move(object))
                , on_free(std::move(onFree))
            {
            }
            ~augmented_node()
            {
                if (on_free && owner)
                {
                    on_free(id, std::move(cached_object));
                }
            }

            on_free_fn on_free;
        };
        using node_ptr = std::shared_ptr<node>;
        using node_weak_ptr = std::weak_ptr<node>;


        using access_queue = moodycamel::ConcurrentQueue<node_ptr>;
        using cache_map = cuckoohash_map<Key, node_weak_ptr, Hash, KeyEqual, Allocator, SLOT_PER_BUCKET>;

    public:
        cache(is_dirty_fn isDirtyPredicate)
            : is_dirty(std::move(isDirtyPredicate))
        {
            if (!is_dirty)
            {
                BOOST_THROW_EXCEPTION(logic_error{});
            }
        }
        ~cache()
        {
            // this MUST be done HERE explicitly, otherwise the invocation of on_node_destruction()
            // from any of the node destructors is UB
            clear();
        }

        handle try_access(const key_type &id)
        {
            handle h;
            node_ptr n;
            mCachedValues.find_fn(id, [&n](const node_weak_ptr &weak_node)
            {
                n = weak_node.lock();
            });
            if (n)
            {
                h = n->cached_object;
                mAccessQueue.enqueue(std::move(n));
            }
            return h;
        }

        void make_room()
        {
            for (std::size_t i = 0, limit = mAccessQueue.size_approx(); i < limit; ++i)
            {
                node_ptr elem;
                mAccessQueue.try_dequeue(elem);
                if (elem && is_dirty(elem->cached_object) && elem.use_count() == 1)
                {
                    mAccessQueue.enqueue(std::move(elem));
                }
                else
                {
                    node_weak_ptr postMortem{ elem };
                    elem.reset();
                    // check wether the above reset killed the kid
                    if (postMortem.expired())
                    {
                        break;
                    }
                }
            }
        }

        std::tuple<bool, handle> try_push(key_type id, handle obj)
        {
            return try_push(
                std::make_shared<node>(this, std::move(id), std::move(obj))
            );
        }

        std::tuple<bool, handle> try_push(key_type id, handle obj, on_free_fn onFree)
        {
            return try_push(
                std::static_pointer_cast<node>(
                    std::make_shared<augmented_node>(this, std::move(id), std::move(obj), std::move(onFree))
                )
            );
        }

        void purge(const key_type &id)
        {
            mCachedValues.erase(id);
        }
        void purge(const key_type &id, const handle &obj)
        {
            mCachedValues.erase_fn(id, [&obj](node_weak_ptr &wnode)
            {
                if (auto node = wnode.lock(); node && node->cached_object != obj)
                {
                    // the key got overwritten, so we don't erase the new obj
                    return false;
                }
                return true;
            });
        }

        // this is a semi thread safe function
        // while the cache will always stay in a valid state, the behaviour of accessing
        // the cache during a clear operation can only be described as _undesirable_
        void clear()
        {
            while (!mCachedValues.empty())
            {
                make_room();
                std::this_thread::yield();
            }
        }

    private:
        std::tuple<bool, handle> try_push(node_ptr n)
        {
            bool existing = false;
            mCachedValues.uprase_fn(n->id, [&existing, &n](node_weak_ptr &mapNode)
            {
                if (auto existingNode = mapNode.lock())
                {
                    existing = true;
                    // prevent on_node_destruction call
                    n->on_owner_released();
                    n = std::move(existingNode);
                }
                else
                {
                    mapNode = n;
                }
                return false;
            }, n);

            handle resource = n->cached_object;
            mAccessQueue.enqueue(std::move(n));
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

        access_queue mAccessQueue;
        cache_map mCachedValues;
        is_dirty_fn is_dirty;
    };
}
