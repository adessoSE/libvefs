#pragma once

#include <boost/predef.h>

#if defined BOOST_COMP_MSVC_AVAILABLE
#include <malloc.h>
#elif defined BOOST_OS_UNIX_AVAILABLE
#include <stdlib.h>
#else
#error "allocator is not implemented for this OS <-> compiler combination"
#endif

#include <cstddef>
#include <cstdlib>

#include <system_error>

#include <vefs/allocator/allocation.hpp>
#include <vefs/disappointment.hpp>

namespace vefs::detail
{
    template <std::size_t ALIGNMENT = alignof(std::max_align_t)>
    class system_allocator
    {
    public:
        static constexpr std::size_t alignment = std::max(ALIGNMENT, alignof(std::max_align_t));
        static constexpr bool is_thread_safe = true;

    private:
        static constexpr bool over_aligned = alignment > alignof(std::max_align_t);

    public:
        inline auto allocate(const std::size_t size) noexcept -> allocation_result;
        inline auto reallocate(const memory_allocation memblock, const std::size_t newSize) noexcept
            -> allocation_result;
        inline void deallocate(memory_allocation memblock) noexcept;
    };

#if defined BOOST_COMP_MSVC_AVAILABLE
    template <std::size_t ALIGNMENT>
    inline auto system_allocator<ALIGNMENT>::allocate(const std::size_t size) noexcept
        -> allocation_result
    {
        if (size == 0)
        {
            return outcome::success(memory_allocation());
        }

        std::byte *ptr;
        if constexpr (over_aligned)
        {
            ptr = reinterpret_cast<std::byte *>(_aligned_malloc(size, alignment));
        }
        else
        {
            ptr = reinterpret_cast<std::byte *>(std::malloc(size));
        }
        if (!ptr)
        {
            return errc::not_enough_memory;
        }
        return {memory_allocation{ptr, size}};
    }

    template <std::size_t ALIGNMENT>
    inline auto system_allocator<ALIGNMENT>::reallocate(const memory_allocation memblock,
                                                        const std::size_t newSize) noexcept
        -> allocation_result
    {
        if (newSize == 0)
        {
            deallocate(memblock);
            return outcome::success(memory_allocation());
        }

        std::byte *ptr;
        if constexpr (over_aligned)
        {
            ptr =
                reinterpret_cast<std::byte *>(_aligned_realloc(memblock.raw(), newSize, alignment));
        }
        else
        {
            ptr = reinterpret_cast<std::byte *>(std::realloc(memblock.raw(), newSize));
        }
        if (!ptr)
        {
            return errc::not_enough_memory;
        }
        return {memory_allocation{ptr, newSize}};
    }

    template <std::size_t ALIGNMENT>
    inline void system_allocator<ALIGNMENT>::deallocate(memory_allocation memblock) noexcept
    {
        if (memblock.raw() != nullptr)
        {
            if constexpr (over_aligned)
            {
                _aligned_free(memblock.raw());
            }
            else
            {
                std::free(memblock.raw());
            }
        }
    }
#elif defined BOOST_OS_UNIX_AVAILABLE
    template <std::size_t ALIGNMENT>
    inline auto system_allocator<ALIGNMENT>::allocate(const std::size_t size) noexcept
        -> allocation_result
    {
        if (size == 0)
        {
            return outcome::success(memory_allocation());
        }

        std::byte *ptr;
        if constexpr (over_aligned)
        {
            void *tmp = nullptr;
            if (auto merrno = posix_memalign(&tmp, size, alignment); merrno != 0)
            {
                if (merrno == ENOMEM)
                {
                    return errc::not_enough_memory;
                }
                return outcome::failure(std::error_code{merrno, std::system_category()});
            }
            ptr = reinterpret_cast<std::byte *>(tmp);
        }
        else
        {
            ptr = reinterpret_cast<std::byte *>(malloc(size));
            if (!ptr)
            {
                return errc::not_enough_memory;
            }
        }
        return {memory_allocation{ptr, size}};
    }

    template <std::size_t ALIGNMENT>
    inline auto system_allocator<ALIGNMENT>::reallocate(const memory_allocation memblock,
                                                        const std::size_t newSize) noexcept
        -> allocation_result
    {
        if (newSize == 0)
        {
            deallocate(memblock);
            return outcome::success(memory_allocation());
        }

        std::byte *ptr;
        if constexpr (over_aligned)
        {
            static_assert(!over_aligned,
                          "posix doesn't specify a way to reallocate over aligned memory, sorry.");
        }
        else
        {
            ptr = reinterpret_cast<std::byte *>(std::realloc(memblock.raw(), newSize));
        }
        if (!ptr)
        {
            return errc::not_enough_memory;
        }
        return {memory_allocation{ptr, newSize}};
    }

    template <std::size_t ALIGNMENT>
    inline void system_allocator<ALIGNMENT>::deallocate(memory_allocation memblock) noexcept
    {
        std::free(memblock.raw());
    }
#endif

    using default_system_allocator = system_allocator<>;
} // namespace vefs::detail
