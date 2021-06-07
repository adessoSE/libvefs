#pragma once

#include <cstddef>

#include <span>

#include <dplx/dp/customization.hpp>

#include <vefs/utils/secure_array.hpp>

namespace dplx::dp
{

template <typename T, std::size_t N>
inline auto tag_invoke(container_reserve_fn,
                       vefs::utils::secure_array<T, N> &c,
                       std::size_t size) noexcept -> result<void>
{
    if (size <= c.size())
    {
        return oc::success();
    }
    return errc::not_enough_memory;
}
template <typename T, std::size_t N>
inline auto tag_invoke(container_resize_fn,
                       vefs::utils::secure_array<T, N> &c,
                       std::size_t size) noexcept -> result<void>
{
    if (size <= c.size())
    {
        return oc::success();
    }
    return errc::not_enough_memory;
}

} // namespace dplx::dp
