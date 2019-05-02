#pragma once

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <optional>
#include <system_error>

#include <boost/predef.h>
#include <boost/throw_exception.hpp>

#if defined BOOST_COMP_MSVC_AVAILABLE
#include <malloc.h>
#elif defined BOOST_OS_UNIX_AVAILABLE
#include <stdlib.h>
#else
#error "allocator is not implemented for this OS <-> compiler combination"
#endif

#include <vefs/blob.hpp>
#include <vefs/utils/bit_scan.hpp>
#include <vefs/utils/misc.hpp>

namespace vefs::utils::detail
{
    template <std::size_t ELEM_SIZE, std::size_t ALIGNMENT>
    struct alignmnent_values
    {
        static constexpr std::size_t alignment = ALIGNMENT;
        static constexpr std::size_t elem_size = ELEM_SIZE;

        static constexpr std::size_t adj_elem_size =
            div_ceil(std::max(elem_size, std::size_t{1}), alignment) * alignment;
        static constexpr std::size_t adj_elem_overhead = adj_elem_size - elem_size;
    };
} 

namespace vefs::utils
{
    struct memory_allocation
    {
        inline memory_allocation(std::byte *const start, std::byte *const end) noexcept
            : mStart(start)
            , mEnd(end)
        {
        }
        inline memory_allocation(std::byte *const start, const std::size_t size) noexcept
            : memory_allocation(start, start + size)
        {
        }

        inline void *raw() const
        {
            return mStart;
        }
        inline void *raw_end() const
        {
            return mEnd;
        }
        inline std::size_t size() const
        {
            return static_cast<std::size_t>(std::distance(mStart, mEnd));
        }

        inline rw_dynblob data() const
        {
            return rw_dynblob{mStart, size()};
        }
        inline ro_dynblob view() const
        {
            return ro_dynblob{mStart, size()};
        }

    private:
        std::byte *mStart;
        std::byte *mEnd;
    };

    using maybe_allocation = std::optional<memory_allocation>;
    constexpr auto failed_allocation = std::nullopt;

    template <std::size_t ALIGNMENT = alignof(std::max_align_t)>
    class system_allocator
    {
    public:
        static constexpr std::size_t alignment = std::max(ALIGNMENT, alignof(std::max_align_t));

    private:
        static constexpr bool over_aligned = alignment > alignof(std::max_align_t);
        static constexpr bool is_thread_safe = true;

    public:
#if defined BOOST_COMP_MSVC_AVAILABLE
        [[nodiscard]] inline maybe_allocation allocate(const std::size_t size)
        {
            std::byte *ptr;
            if constexpr (over_aligned)
            {
                ptr = reinterpret_cast<std::byte *>(_aligned_malloc(size, alignment));
            }
            else
            {
                ptr = reinterpret_cast<std::byte *>(malloc(size));
            }
            if (!ptr)
            {
                if (errno != ENOMEM)
                {
                    BOOST_THROW_EXCEPTION(std::system_error(errno, std::generic_category(),
                                                            "failed to allocate memory due to an unexpected error"));
                }
                return failed_allocation;
            }
            return {{ptr, size}};
        }
        [[nodiscard]] inline maybe_allocation reallocate(const memory_allocation memblock, const std::size_t newSize)
        {
            std::byte *ptr;
            if constexpr (over_aligned)
            {
                ptr = reinterpret_cast<std::byte *>(_aligned_realloc(memblock.raw(), newSize, alignment));
            }
            else
            {
                ptr = reinterpret_cast<std::byte *>(realloc(memblock.raw(), newSize));
            }
            if (!ptr)
            {
                if (errno != ENOMEM)
                {
                    BOOST_THROW_EXCEPTION(std::system_error(errno, std::generic_category(),
                                                            "failed to allocate memory due to an unexpected error"));
                }
                return failed_allocation;
            }
            return {{ptr, newSize}};
        }
        inline void deallocate(memory_allocation memblock)
        {
            if constexpr (over_aligned)
            {
                _aligned_free(memblock.raw());
            }
            else
            {
                free(memblock.raw());
            }
        }
#elif defined BOOST_OS_UNIX_AVAILABLE
        [[nodiscard]] inline maybe_allocation allocate(const std::size_t size)
        {
            std::byte *ptr;
            if constexpr (over_aligned)
            {
                void *tmp = nullptr;
                if (auto merrno = posix_memalign(&tmp, size, alignment); merrno != 0)
                {
                    BOOST_THROW_EXCEPTION(std::system_error(merrno, std::generic_category(),
                                                            "failed to allocate memory due to an unexpected error"));
                }
            }
            else
            {
                ptr = reinterpret_cast<std::byte *>(malloc(size));
                if (!ptr)
                {
                    if (errno != ENOMEM)
                    {
                        BOOST_THROW_EXCEPTION(std::system_error(
                            errno, std::generic_category(), "failed to allocate memory due to an unexpected error"));
                    }
                    return failed_allocation;
                }
            }
            return {{ptr, size}};
        }
        [[nodiscard]] inline maybe_allocation reallocate(const memory_allocation memblock, const std::size_t newSize)
        {
            std::byte *ptr;
            if constexpr (over_aligned)
            {
                static_assert(!over_aligned, "posix doesn't specify a way to reallocate over aligned memory, sorry.");
            }
            else
            {
                ptr = reinterpret_cast<std::byte *>(realloc(memblock.raw(), newSize));
            }
            if (!ptr)
            {
                if (errno != ENOMEM)
                {
                    BOOST_THROW_EXCEPTION(std::system_error(errno, std::generic_category(),
                                                            "failed to allocate memory due to an unexpected error"));
                }
                return failed_allocation;
            }
            return {{ptr, newSize}};
        }
        inline void deallocate(memory_allocation memblock)
        {
            free(memblock.raw());
        }
#endif
    };

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push)
#pragma warning(                                                                                                       \
    disable : 4584) // class appearing multiple times in the inheritance hierarchy which is intended in this case
#endif

    template <typename Primary, typename... Fallbacks>
    class octopus_allocator
        : private Primary
        , private Fallbacks...
    {
        template <typename Current, typename... Remaining>
        inline maybe_allocation allocate(const std::size_t size)
        {
            if constexpr (sizeof...(Remaining) > 0)
            {
                if (auto memblock = Current::allocate(size); memblock != failed_allocation)
                {
                    return memblock;
                }
                return octopus_allocator::allocate<Remaining...>(size);
            }
            else
            {
                return Current::allocate(size);
            }
        }

        template <typename Owner, typename Current, typename... Remaining>
        inline maybe_allocation relocate(const memory_allocation memblock, const std::size_t size)
        {
            if (maybe_allocation reloc = Current::allocate(size))
            {
                auto moveSize = std::min(memblock.size(), size);
                std::memmove(reloc.raw(), memblock.raw(), moveSize);

                Owner::deallocate(memblock);
                return reloc;
            }

            if constexpr (sizeof...(Remaining) > 0)
            {
                return relocate<Owner, Remaining...>(memblock, size);
            }
            else
            {
                return failed_allocation;
            }
        }

        template <typename Current, typename... Remaining>
        inline maybe_allocation reallocate(const memory_allocation memblock, const std::size_t size)
        {
            if constexpr (sizeof...(Remaining) > 0)
            {
                if (Current::owns(memblock))
                {
                    if (auto reallocated = Current::reallocate(memblock, size))
                    {
                        return reallocated;
                    }
                    return octopus_allocator::relocate<Current, Remaining...>(memblock, size);
                }
                return octopus_allocator::reallocate<Remaining...>(memblock, size);
            }
            else
            {
                return Current::reallocate(memblock, size);
            }
        }

        template <typename Current, typename... Remaining>
        inline void deallocate(memory_allocation memblock)
        {
            if constexpr (sizeof...(Remaining) > 0)
            {
                if (Current::owns(memblock))
                {
                    Current::deallocate(memblock);
                }
                else
                {
                    octopus_allocator::deallocate<Remaining...>(memblock);
                }
            }
            else
            {
                Current::deallocate(memblock);
            }
        }

    public:
        static constexpr std::size_t alignment = std::min({Primary::alignment, Fallbacks::alignment...});

        [[nodiscard]] inline maybe_allocation allocate(const std::size_t size)
        {
            return octopus_allocator::allocate<Primary, Fallbacks...>(size);
        }
        [[nodiscard]] inline maybe_allocation reallocate(const memory_allocation memblock, const std::size_t size)
        {
            return octopus_allocator::reallocate<Primary, Fallbacks...>(memblock, size);
        }
        inline void deallocate(memory_allocation memblock)
        {
            octopus_allocator::deallocate<Primary, Fallbacks...>(memblock);
        }

        inline bool owns(const memory_allocation memblock)
        {
            return Primary::owns(memblock) || (... || Fallbacks::owns(memblock));
        }
    };

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif

    using default_system_allocator = system_allocator<>;

    template <typename T, typename Allocator>
    class alloc_std_adaptor
    {
        using impl_t = Allocator;

        // necessary for copy constructors
        template <typename, typename>
        friend class alloc_std_adaptor;

        template <typename U, typename V, typename Allocator>
        friend bool operator==(const alloc_std_adaptor<U, Allocator> &lhs, const alloc_std_adaptor<V, Allocator> &rhs);

    public:
        // this is problematic if alloc_std_adaptor is part of T
        // I ran into this issue, when using it in conjunction with std::allocate_shared
        // static_assert(alignof(T) <= impl_t::alignment);

        using value_type = T;
        using impl_handle = std::shared_ptr<impl_t>;

        using propagate_on_container_copy_assignment = std::true_type;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_swap = std::true_type;
        using is_always_equal = std::false_type;

        inline alloc_std_adaptor()
            : mAllocator{std::make_shared<impl_t>()}
        {
        }
        inline alloc_std_adaptor(impl_handle h)
            : mAllocator{std::move(h)}
        {
            assert(mAllocator);
        }
        inline alloc_std_adaptor(const alloc_std_adaptor &other)
            : alloc_std_adaptor{other.mAllocator}
        {
        }
        inline alloc_std_adaptor(alloc_std_adaptor &&other)
            : alloc_std_adaptor{other.mAllocator}
        {
        }
        template <typename U>
        inline alloc_std_adaptor(const alloc_std_adaptor<U, impl_t> &other)
            : alloc_std_adaptor{other.mAllocator}
        {
        }
        template <typename U>
        inline alloc_std_adaptor(alloc_std_adaptor<U, impl_t> &&other)
            : alloc_std_adaptor{other.mAllocator}
        {
        }

        inline alloc_std_adaptor &operator=(const alloc_std_adaptor &other)
        {
            mAllocator = other.mAllocator;
            return *this;
        }
        inline alloc_std_adaptor &operator=(alloc_std_adaptor &&other)
        {
            mAllocator = other.mAllocator;
            return *this;
        }
        template <typename U>
        inline alloc_std_adaptor &operator=(const alloc_std_adaptor<U, impl_t> &other)
        {
            mAllocator = other.mAllocator;
            return *this;
        }
        template <typename U>
        inline alloc_std_adaptor &operator=(alloc_std_adaptor<U, impl_t> &&other)
        {
            mAllocator = other.mAllocator;
            return *this;
        }

        [[nodiscard]] inline value_type *allocate(std::size_t n)
        {
            if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            {
                // n * sizeof(T) would overflow...
                BOOST_THROW_EXCEPTION(std::bad_alloc{});
            }
            if (auto mblk = mAllocator->allocate(n * sizeof(T)))
            {
                return reinterpret_cast<value_type *>(mblk->raw());
            }
            BOOST_THROW_EXCEPTION(std::bad_alloc{});
        }

        inline void deallocate(T *p, std::size_t n)
        {
            mAllocator->deallocate({reinterpret_cast<std::byte *>(p), n * sizeof(T)});
        }

        friend void swap(alloc_std_adaptor &l, alloc_std_adaptor &r) noexcept
        {
            using std::swap;
            swap(l.mAllocator, r.mAllocator);
        }

        template <typename U>
        inline bool operator==(const alloc_std_adaptor<U, Allocator> &rhs) const noexcept
        {
            return mAllocator == rhs.mAllocator;
        }

    private:
        impl_handle mAllocator;
    };

    template <typename T, typename U, typename Allocator>
    inline bool operator!=(const alloc_std_adaptor<T, Allocator> &lhs,
                           const alloc_std_adaptor<U, Allocator> &rhs) noexcept
    {
        return !(lhs == rhs);
    }
} // namespace vefs::utils
