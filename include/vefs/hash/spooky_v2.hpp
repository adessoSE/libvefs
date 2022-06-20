#pragma once

#include <cstddef>
#include <cstdint>

#include <dplx/cncr/math_supplement.hpp>

#include <vefs/hash/detail/spooky_v2_impl.hpp>
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
    static void generate_keys(std::span<key_type> const keys) noexcept;

    void update(std::byte const *const data,
                std::size_t const byteSize) noexcept
    {
        mState.Update(data, byteSize);
    }

    template <dplx::cncr::unsigned_integer H>
        requires(4 == sizeof(H) || sizeof(H) == 8)
    [[nodiscard]] auto final() noexcept -> H
    {
        std::uint64_t h1{};
        std::uint64_t h2{};
        mState.Final(&h1, &h2);
        return static_cast<H>(h1);
    }

    template <dplx::cncr::unsigned_integer H>
        requires(4 == sizeof(H) || sizeof(H) == 8)
    [[nodiscard]] static auto hash(std::byte const *const data,
                                   std::size_t const byteSize) noexcept -> H
    {
        std::uint64_t h1{};
        std::uint64_t h2{};
        external::SpookyHash::Hash128(data, byteSize, &h1, &h2);
        return static_cast<H>(h1);
    }
    template <dplx::cncr::unsigned_integer H>
        requires(4 == sizeof(H) || sizeof(H) == 8)
    [[nodiscard]] static auto hash(key_type const key,
                                   std::byte const *const data,
                                   std::size_t const byteSize) noexcept -> H
    {
        std::uint64_t h1{key.part1};
        std::uint64_t h2{key.part2};
        external::SpookyHash::Hash128(data, byteSize, &h1, &h2);
        return static_cast<H>(h1);
    }
};

} // namespace vefs
