#pragma once

#include <cstdint>

#include <array>
#include <bit>
#include <concepts>
#include <span>
#include <type_traits>

#include <dplx/cncr/math_supplement.hpp>
#include <dplx/cncr/tag_invoke.hpp>

namespace vefs
{

// clang-format off
template <typename T>
concept hash_algorithm
    = std::semiregular<T>
    && requires(T &state,
                std::byte const *data,
                std::size_t byteSize)
{
    { T::template hash<std::uint32_t>(data, byteSize) } noexcept
        -> std::same_as<std::uint32_t>;
    { T::template hash<std::uint64_t>(data, byteSize) } noexcept
        -> std::same_as<std::uint64_t>;
    { T::template hash<std::size_t>(data, byteSize) } noexcept
        -> std::same_as<std::size_t>;

    { state.update(data, byteSize) } noexcept;
    { state.template final<std::uint32_t>() } noexcept
        -> std::same_as<std::uint32_t>;
    { state.template final<std::uint64_t>() } noexcept
        -> std::same_as<std::uint64_t>;
};
// clang-format on

// clang-format off
template <typename T>
concept keyable_hash_algorithm
    = hash_algorithm<T>
    && std::constructible_from<T, typename T::key_type const &>
    && requires(typename T::key_type const &key,
                std::span<typename T::key_type> const &keys,
                std::byte const *data,
                std::size_t byteSize)
{
    typename T::key_type;

    { T::generate_key() }
        -> std::same_as<typename T::key_type>;
    T::generate_keys(keys);

    { T::template hash<std::uint32_t>(key, data, byteSize) } noexcept
        -> std::same_as<std::uint32_t>;
    { T::template hash<std::uint64_t>(key, data, byteSize) } noexcept
        -> std::same_as<std::uint64_t>;
    { T::template hash<std::size_t>(key, data, byteSize) } noexcept
        -> std::same_as<std::size_t>;
};
// clang-format on

template <typename T>
inline constexpr bool disable_trivially_hashable = false;

template <typename T>
concept trivially_hashable = std::has_unique_object_representations_v<
        T> && !disable_trivially_hashable<T>;

inline constexpr struct hash_update_fn
{
    template <hash_algorithm Algorithm, typename T>
        requires dplx::cncr::
                nothrow_tag_invocable<hash_update_fn, Algorithm &, T const &>
    constexpr void operator()(Algorithm &hashState,
                              T const &object) const noexcept
    {
        dplx::cncr::tag_invoke(*this, hashState, object);
    }

    template <hash_algorithm Algorithm, trivially_hashable T>
    inline friend constexpr void
    tag_invoke(hash_update_fn, Algorithm &hashState, T const &object) noexcept
    {
        if (std::is_constant_evaluated())
        {
            std::array<std::byte, sizeof(T)> const bits
                    = std::bit_cast<std::array<std::byte, sizeof(T)>>(object);
            hashState.update(bits.data(), sizeof(object));
        }
        else
        {
            hashState.update(reinterpret_cast<std::byte const *>(&object),
                             sizeof(object));
        }
    }
} hash_update{};

template <typename T, typename Algorithm>
concept hashable
        = dplx::cncr::tag_invocable<hash_update_fn, Algorithm &, T const &>;

template <hash_algorithm Algorithm, dplx::cncr::unsigned_integer H>
struct hash_fn
{
    template <typename T>
        requires dplx::cncr::nothrow_tag_invocable<hash_fn, T const &>
    [[nodiscard]] constexpr auto operator()(T const &object) const noexcept
            -> dplx::cncr::tag_invoke_result_t<hash_fn, T const &>
    {
        return dplx::cncr::tag_invoke(*this, object);
    }

    template <hashable<Algorithm> T>
        requires(!dplx::cncr::nothrow_tag_invocable<hash_fn, T const &>)
    [[nodiscard]] constexpr auto operator()(T const &object) const noexcept
            -> dplx::cncr::tag_invoke_result_t<hash_fn, T const &>
    {
        Algorithm state;
        hash_update(state, object);
        return state.template final<H>();
    }

    template <trivially_hashable T>
    friend inline constexpr auto tag_invoke(hash_fn, T const &object) noexcept
            -> H
    {
        if (std::is_constant_evaluated())
        {
            std::array<std::byte, sizeof(T)> const bits
                    = std::bit_cast<std::array<std::byte, sizeof(T)>>(object);
            return Algorithm::template hash<H>(bits.data(), sizeof(object));
        }
        else
        {
            return Algorithm::template hash<H>(
                    reinterpret_cast<std::byte const *>(&object),
                    sizeof(object));
        }
    }
};
template <keyable_hash_algorithm Algorithm, dplx::cncr::unsigned_integer H>
struct hash_fn<Algorithm, H>
{
    template <typename T>
        requires dplx::cncr::nothrow_tag_invocable<hash_fn, T const &>
    [[nodiscard]] constexpr auto operator()(T const &object) const noexcept
            -> dplx::cncr::tag_invoke_result_t<hash_fn, T const &>
    {
        return dplx::cncr::tag_invoke(*this, object);
    }
    template <typename T>
        requires dplx::cncr::nothrow_tag_invocable<
                hash_fn,
                typename Algorithm::key_type const &,
                T const &>
    [[nodiscard]] constexpr auto
    operator()(typename Algorithm::key_type const &key,
               T const &object) const noexcept
            -> dplx::cncr::tag_invoke_result_t<
                    hash_fn,
                    typename Algorithm::key_type const &,
                    T const &>
    {
        return dplx::cncr::tag_invoke(*this, key, object);
    }

    template <hashable<Algorithm> T>
        requires(!dplx::cncr::nothrow_tag_invocable<hash_fn, T const &>)
    [[nodiscard]] constexpr auto operator()(T const &object) const noexcept
            -> dplx::cncr::tag_invoke_result_t<hash_fn, T const &>
    {
        Algorithm state;
        hash_update(state, object);
        return state.template final<H>();
    }
    template <hashable<Algorithm> T>
        requires(!dplx::cncr::nothrow_tag_invocable<
                 hash_fn,
                 typename Algorithm::key_type const &,
                 T const &>)
    [[nodiscard]] constexpr auto
    operator()(typename Algorithm::key_type const &key,
               T const &object) const noexcept
            -> dplx::cncr::tag_invoke_result_t<
                    hash_fn,
                    typename Algorithm::key_type const &,
                    T const &>
    {
        Algorithm state(key);
        hash_update(state, object);
        return state.template final<H>();
    }

    template <trivially_hashable T>
    friend inline constexpr auto tag_invoke(hash_fn, T const &object) noexcept
            -> H
    {
        if (std::is_constant_evaluated())
        {
            std::array<std::byte, sizeof(T)> const bits
                    = std::bit_cast<std::array<std::byte, sizeof(T)>>(object);
            return Algorithm::template hash<H>(bits.data(), sizeof(object));
        }
        else
        {
            return Algorithm::template hash<H>(
                    reinterpret_cast<std::byte const *>(&object),
                    sizeof(object));
        }
    }
    template <trivially_hashable T>
    friend inline constexpr auto
    tag_invoke(hash_fn,
               typename Algorithm::key_type const &key,
               T const &object) noexcept -> H
    {
        if (std::is_constant_evaluated())
        {
            std::array<std::byte, sizeof(T)> const bits
                    = std::bit_cast<std::array<std::byte, sizeof(T)>>(object);
            return Algorithm::template hash<H>(key, bits.data(),
                                               sizeof(object));
        }
        else
        {
            return Algorithm::template hash<H>(
                    key, reinterpret_cast<std::byte const *>(&object),
                    sizeof(object));
        }
    }
};

template <hash_algorithm Algorithm, dplx::cncr::unsigned_integer H>
inline constexpr hash_fn<Algorithm, H> hash{};

template <hash_algorithm Algorithm, hashable<Algorithm> T>
struct std_hash_for
{
    auto operator()(T const &v) const noexcept -> std::size_t
    {
        return hash<Algorithm, std::size_t>(v);
    }
};

} // namespace vefs
