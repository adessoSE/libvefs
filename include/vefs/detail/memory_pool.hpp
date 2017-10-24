#pragma once

#include <cstddef>

#include <array>
#include <limits>
#include <atomic>
#include <bitset>

#include <vefs/exceptions.hpp>
#include <vefs/ext/concurrentqueue/concurrentqueue.h>

namespace vefs::detail
{

    template <std::size_t ChunkSize, std::size_t NumChunks>
    class block_memory_pool
    {
    public:
        static constexpr std::size_t chunk_size = ChunkSize;
        static constexpr std::size_t num_chunks = NumChunks;

        block_memory_pool()
        {
            const auto ptr = mMemory.data();
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

        blob alloc()
        {
            std::byte *ptr = nullptr;
            mFreeChunks.try_dequeue(ptr);
            return blob{ ptr, chunk_size };
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
        std::array<std::byte, chunk_size * num_chunks> mMemory;
        moodycamel::ConcurrentQueue<std::byte *> mFreeChunks;
    };
}
