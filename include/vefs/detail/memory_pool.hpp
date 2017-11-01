#pragma once

#include <cstddef>

#include <array>
#include <limits>
#include <atomic>
#include <bitset>
#include <utility>

#include <vefs/exceptions.hpp>
#include <vefs/ext/concurrentqueue/concurrentqueue.h>

namespace vefs::detail
{

    template <std::size_t ChunkSize, std::size_t NumChunks, std::size_t ChunkAlignment = 0>
    class block_memory_pool
    {
    public:
        static constexpr std::size_t chunk_size = ChunkSize;
        static constexpr std::size_t num_chunks = NumChunks;

        using chunk_ptr = std::shared_ptr<blob>;
        using weak_chunk_ptr = chunk_ptr::weak_type;

        block_memory_pool()
        {
            auto ptr = mMemory.data();
            const auto end = ptr + chunk_size * num_chunks;
            for (; ptr < end; ptr += chunk_size)
            {
                mFreeChunks.enqueue(ptr);
            }
        }
        block_memory_pool(const block_memory_pool &) = delete;
        block_memory_pool(block_memory_pool &&) = delete;
        block_memory_pool & operator=(const block_memory_pool &) = delete;
        block_memory_pool & operator=(block_memory_pool &&) = delete;

        blob try_alloc()
        {
            std::byte *ptr = nullptr;
            mFreeChunks.try_dequeue(ptr);
            return blob{ ptr, chunk_size };
        }
        chunk_ptr try_alloc_shared()
        {
            struct owning_blob : blob
            {
                owning_blob(blob mem, block_memory_pool pool)
                    : blob(mem)
                    , pool(pool)
                {
                }
                ~owning_blob()
                {
                    pool->dealloc(*this);
                }
                block_memory_pool *pool;
            };
            auto mem = try_alloc();
            if (!mem)
            {
                return chunk_ptr{};
            }
            return std::static_pointer_cast<blob>(
                std::make_shared<owning_blob>(mem, this)
            );
        }
        void dealloc(blob mem)
        {
            const auto ptr = mem.data();
            if (mem.size() != chunk_size
                || !(ptr >= &mMemory.front() && ptr <= &mMemory.back()))
            {
                BOOST_THROW_EXCEPTION(logic_error{});
            }
            const auto offset = static_cast<size_t>(ptr - mMemory.data());
            if (offset % chunk_size != 0)
            {
                // the pointer is not aligned with any element position
                BOOST_THROW_EXCEPTION(logic_error{});
            }
            mFreeChunks.enqueue(ptr);
        }

    private:
        moodycamel::ConcurrentQueue<std::byte *> mFreeChunks;
        alignas(ChunkAlignment) std::array<std::byte, chunk_size * num_chunks> mMemory;
    };

    template <typename T, std::size_t NumChunks>
    class shared_object_pool
    {
        static constexpr std::size_t alignment_mismatch = sizeof(T) % alignof(T);
        static constexpr std::size_t block_size
            = sizeof(T) + !!alignment_mismatch * (alignof(T) - alignment_mismatch);
    public:
        using handle = std::shared_ptr<T>;

        template <typename... Args>
        handle create(Args&&... args)
        {
            auto mem = mMemPool.try_alloc();
            if (!mem)
            {
                return std::shared_ptr<T>{};
            }
            return std::shared_ptr<T>{
                new (mem.data()) T(std::forward<Args>(args)...),
                [this](T *obj)
                {
                    if (obj)
                    {
                        obj->~T();
                        mMemPool.dealloc({ reinterpret_cast<std::byte *>(obj), block_size });
                    }
                }
            };
        }

    private:
        block_memory_pool<block_size, NumChunks, alignof(T)> mMemPool;
    };
}
