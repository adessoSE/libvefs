#pragma once

#include <memory>
#include <vector>

#include <vefs/utils/secure_ops.hpp>

namespace vefs::utils
{
    template <typename T>
    class secure_allocator : public std::allocator<T>
    {
    public:
        template <typename U>
        using rebind = secure_allocator<U>;

        secure_allocator() noexcept = default;
        secure_allocator(const secure_allocator &) noexcept = default;
        template <typename U>
        secure_allocator(const secure_allocator<U> &) noexcept
        {
        }

        void deallocate(T *p, std::size_t num)
        {
            secure_memzero(as_writable_bytes(span(p, num)));
            std::allocator<T>::deallocate(p, num);
        }
    };

    template <typename T>
    using secure_vector = std::vector<T, secure_allocator<T>>;
} // namespace vefs::utils
