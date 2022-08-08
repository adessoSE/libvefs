#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <vefs/hash/hash_algorithm.hpp>
#include <vefs/hash/spooky_v2.hpp>

namespace vefs
{

template <hash_algorithm Algorithm, typename Char, typename Traits>
inline void tag_invoke(hash_update_fn,
                       Algorithm &hashState,
                       std::basic_string_view<Char, Traits> str) noexcept
{
    if (!str.empty())
    {
        hashState.update(reinterpret_cast<std::byte const *>(str.data()),
                         str.size());
    }
}
template <hash_algorithm Algorithm,
          dplx::cncr::unsigned_integer H,
          typename Char,
          typename Traits>
inline auto tag_invoke(hash_fn<Algorithm, H>,
                       std::basic_string_view<Char, Traits> const &str) noexcept
        -> H
{
    return Algorithm::template hash<H>(
            reinterpret_cast<std::byte const *>(str.data()), str.size());
}
template <keyable_hash_algorithm Algorithm,
          dplx::cncr::unsigned_integer H,
          typename Char,
          typename Traits>
inline auto tag_invoke(hash_fn<Algorithm, H>,
                       typename Algorithm::key_type const &key,
                       std::basic_string_view<Char, Traits> const &str) noexcept
        -> H
{
    return Algorithm::template hash<H>(
            key, reinterpret_cast<std::byte const *>(str.data()), str.size());
}
template <typename Char, typename Traits>
inline constexpr bool
        disable_trivially_hashable<std::basic_string_view<Char, Traits>> = true;

template <hash_algorithm Algorithm, typename Char, typename Traits>
inline void tag_invoke(hash_update_fn,
                       Algorithm &hashState,
                       std::basic_string<Char, Traits> str) noexcept
{
    hashState.update(reinterpret_cast<std::byte const *>(str.data()),
                     str.size());
}
template <hash_algorithm Algorithm,
          dplx::cncr::unsigned_integer H,
          typename Char,
          typename Traits>
inline auto tag_invoke(hash_fn<Algorithm, H>,
                       std::basic_string<Char, Traits> const &str) noexcept -> H
{
    return Algorithm::template hash<H>(
            reinterpret_cast<std::byte const *>(str.data()), str.size());
}
template <keyable_hash_algorithm Algorithm,
          dplx::cncr::unsigned_integer H,
          typename Char,
          typename Traits>
inline auto tag_invoke(hash_fn<Algorithm, H>,
                       typename Algorithm::key_type const &key,
                       std::basic_string<Char, Traits> const &str) noexcept -> H
{
    return Algorithm::template hash<H>(
            key, reinterpret_cast<std::byte const *>(str.data()), str.size());
}

} // namespace vefs
