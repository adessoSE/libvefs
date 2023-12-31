#pragma once

#include <memory>
#include <vector>

#include <vefs/platform/secure_memzero.hpp>

namespace vefs::utils
{
template <typename T>
class secure_allocator : public std::allocator<T>
{
public:
    template <typename U>
    using rebind = secure_allocator<U>;

    secure_allocator() noexcept = default;
    secure_allocator(secure_allocator const &) noexcept = default;
    template <typename U>
    secure_allocator(secure_allocator<U> const &) noexcept
    {
    }

    void deallocate(T *p, std::size_t num)
    {
        secure_memzero(as_writable_bytes(std::span(p, num)));
        std::allocator<T>::deallocate(p, num);
    }
};

template <typename T>
using secure_vector = std::vector<T, secure_allocator<T>>;
} // namespace vefs::utils
