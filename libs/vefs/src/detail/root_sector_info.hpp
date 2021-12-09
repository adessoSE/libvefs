#pragma once

#include <cstddef>
#include <cstdint>

#include <array>

#include "sector_id.hpp"

namespace adesso::vefs
{
class FileDescriptor;
}

namespace vefs::detail
{

struct sector_reference
{
    sector_id sector;
    std::array<std::byte, 16> mac;

    friend constexpr auto operator==(sector_reference,
                                     sector_reference) noexcept -> bool
            = default;
};

struct root_sector_info
{
    sector_reference root;
    std::uint64_t maximum_extent;
    int tree_depth;

    void pack_to(adesso::vefs::FileDescriptor &descriptor);
    static auto unpack_from(adesso::vefs::FileDescriptor &descriptor)
            -> root_sector_info;

    friend constexpr auto operator==(root_sector_info,
                                     root_sector_info) noexcept -> bool
            = default;
};

} // namespace vefs::detail
