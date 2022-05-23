#pragma once

#include <cstddef>

#include <stdexcept>

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>

namespace vefs::detail
{
struct memory_allocation
{
    constexpr memory_allocation() noexcept;
    constexpr memory_allocation(std::byte *const start,
                                std::byte *const end) noexcept;
    constexpr memory_allocation(std::byte *const start,
                                const std::size_t size) noexcept;

    constexpr auto raw() const noexcept -> void *;
    constexpr auto size() const noexcept -> std::size_t;

    constexpr auto bytes() const noexcept -> ro_dynblob;
    constexpr auto writeable_bytes() const noexcept -> rw_dynblob;

private:
    rw_dynblob mSegment;
};

constexpr memory_allocation::memory_allocation() noexcept
    : mSegment{}
{
}
constexpr memory_allocation::memory_allocation(std::byte *const start,
                                               std::byte *const end) noexcept
    : mSegment{start, end}
{
}
constexpr memory_allocation::memory_allocation(std::byte *const start,
                                               const std::size_t size) noexcept
    : mSegment{start, size}
{
}

constexpr auto memory_allocation::raw() const noexcept -> void *
{
    return mSegment.data();
}

constexpr auto memory_allocation::size() const noexcept -> std::size_t
{
    return mSegment.size();
}

constexpr auto memory_allocation::bytes() const noexcept -> ro_dynblob
{
    return mSegment;
}

constexpr auto memory_allocation::writeable_bytes() const noexcept -> rw_dynblob
{
    return mSegment;
}

using allocation_result = result<memory_allocation>;

} // namespace vefs::detail
