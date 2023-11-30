#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <dplx/cncr/math_supplement.hpp>

#include <vefs/hash/detail/spooky_v2_impl.hpp>
#include <vefs/hash/hash_algorithm.hpp>
#include <vefs/span.hpp>

namespace vefs
{

class spooky_v2_hash
{
    external::SpookyHash mState;

public:
    spooky_v2_hash() noexcept
        : mState{}
    {
        mState.Init(0, 0);
    }

    struct key_type
    {
        std::uint64_t part1;
        std::uint64_t part2;
    };

    explicit spooky_v2_hash(key_type const key) noexcept
        : mState{}
    {
        mState.Init(key.part1, key.part2);
    }

    [[nodiscard]] static auto generate_key() noexcept -> key_type;
    static void generate_keys(std::span<key_type> keys) noexcept;

    void update(std::byte const *const data,
                std::size_t const byteSize) noexcept
    {
        mState.Update(data, byteSize);
    }

    template <hash_type H>
    [[nodiscard]] auto final() noexcept -> H
    {
        hash128_t h;
        mState.Final(&h.v[0], &h.v[1]);
        if constexpr (dplx::cncr::unsigned_integer<H>)
        {
            return static_cast<H>(h.v[0]);
        }
        else
        {
            return h;
        }
    }

    template <hash_type H>
    [[nodiscard]] static auto hash(std::byte const *const data,
                                   std::size_t const byteSize) noexcept -> H
    {
        hash128_t h{};
        external::SpookyHash::Hash128(data, byteSize, &h.v[0], &h.v[1]);
        if constexpr (dplx::cncr::unsigned_integer<H>)
        {
            return static_cast<H>(h.v[0]);
        }
        else
        {
            return h;
        }
    }
    template <hash_type H>
    [[nodiscard]] static auto hash(key_type const key,
                                   std::byte const *const data,
                                   std::size_t const byteSize) noexcept -> H
    {
        hash128_t h = std::bit_cast<hash128_t>(key);
        external::SpookyHash::Hash128(data, byteSize, &h.v[0], &h.v[1]);
        if constexpr (dplx::cncr::unsigned_integer<H>)
        {
            return static_cast<H>(h.v[0]);
        }
        else
        {
            return h;
        }
    }
};

} // namespace vefs
