#pragma once

#include <memory>
#include <type_traits>

#include <vefs/detail/string_map.hpp>

#include <vefs/ext/concurrentqueue/concurrentqueue.h>

namespace vefs::detail
{
    template <typename T>
    class cache
    {
    public:
        using id_type = shared_string;
        using handle = std::shared_ptr<T>;
        using weak_handle = typename handle::weak_type;

    private:
        struct node
        {
            node(cache *owner, id_type id, handle object)
                : owner(owner)
                , id(std::move(id))
                , cached_object(std::move(object))
            {
            }
            ~node()
            {
                if (owner)
                {
                    owner->on_node_destruction(id);
                }
            }
            void on_owner_released()
            {
                owner = nullptr;
            }

            cache *owner;
            id_type id;
            handle cached_object;
        };
        using node_ptr = std::shared_ptr<node>;
        using node_weak_ptr = std::weak_ptr<node>;


        using access_queue = moodycamel::ConcurrentQueue<node_ptr>;
        using cache_map = string_map<node_weak_ptr>;

    public:
        ~cache()
        {
            // this MUST be done HERE explicitly, otherwise the invocation of on_node_destruction()
            // from any of the node destructors is UB
            clear();
        }

        handle try_access(const id_type &id)
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
            node_ptr dummy;
            mAccessQueue.try_dequeue(dummy);
        }

        std::tuple<bool, handle> try_push(id_type id, handle obj)
        {
            bool existing = false;
            auto n = std::make_shared<node>(this, std::move(id), std::move(obj));

            mCachedValues.uprase_fn(std::move(id), [&existing, &n](node_weak_ptr &mapNode)
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

        void on_node_destruction(id_type node_id)
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

        // this is a semi thread safe function
        // while the cache will always stay in a valid state, the behaviour of
        // accessing cash during a clear operation can only be described as _undesirable_
        void clear()
        {
            while (!mCachedValues.empty())
            {
                node_ptr n;
                mAccessQueue.try_dequeue(n);
            }
        }

    private:
        access_queue mAccessQueue;
        cache_map mCachedValues;
    };
}
