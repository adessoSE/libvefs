#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <algorithm>
#include <array>
#include <iterator>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace vefs
{
inline constexpr std::size_t dynamic_extent = std::dynamic_extent;

template <class T,
          class U = T,
          std::size_t X = std::dynamic_extent,
          std::size_t Y = X>
    requires std::is_assignable_v<U &, T &>
constexpr auto copy(std::span<T, X> source, std::span<U, Y> dest)
{
    if constexpr (X == std::dynamic_extent || Y == std::dynamic_extent)
    {
        auto const n = std::min(source.size(), dest.size());
        std::copy_n(source.data(), n, dest.data());
        return dest.subspan(n);
    }
    else
    {
        constexpr auto N = std::min(X, Y);
        std::copy_n(source.data(), N, dest.data());
        return dest.template subspan<N>();
    }
}

template <std::size_t Extent>
using rw_blob = std::span<std::byte, Extent>;
using rw_dynblob = rw_blob<dynamic_extent>;

template <std::size_t Extent>
using ro_blob = std::span<const std::byte, Extent>;
using ro_dynblob = ro_blob<dynamic_extent>;

template <std::size_t Extent>
inline void fill_blob(std::span<std::byte, Extent> target,
                      std::byte value = std::byte{})
{
    // calling memset with a nullptr is UB
    if (target.size() > 0)
    {
        std::memset(target.data(), std::to_integer<int>(value), target.size());
    }
}

template <typename T>
    requires(not std::is_const<T>::value)
inline auto rw_blob_cast(T &obj) noexcept -> rw_blob<sizeof(obj)>
{
    return rw_blob<sizeof(obj)>{reinterpret_cast<std::byte *>(&obj),
                                sizeof(obj)};
}
template <typename T>
inline auto ro_blob_cast(const T &obj) noexcept -> ro_blob<sizeof(T)>
{
    return ro_blob<sizeof(T)>{reinterpret_cast<std::byte const *>(&obj),
                              sizeof(obj)};
}

} // namespace vefs
