#pragma once

#include <cassert>
#include <cstddef>

#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <vefs/allocator/allocation.hpp>
#include <vefs/disappointment.hpp>

namespace vefs::detail
{
    template <typename T, typename Allocator>
    class alloc_std_adaptor
    {
        using impl_t = Allocator;

        // necessary for copy constructors
        template <typename, typename>
        friend class alloc_std_adaptor;

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
                std::terminate();
            }
            if (allocation_result mblk = mAllocator->allocate(n * sizeof(T)); mblk.has_value())
            {
                return reinterpret_cast<value_type *>(mblk.assume_value().raw());
            }
            else if (mblk.has_error())
            {
                if (mblk.assume_error() == errc::not_enough_memory)
                {
                    BOOST_THROW_EXCEPTION(std::bad_alloc{});
                }
                throw error_exception{std::move(mblk).assume_error()};
            }
            std::terminate();
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
} // namespace vefs::detail
