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
    };

    class root_sector_info
    {
    public:
        void pack_to(adesso::vefs::FileDescriptor &descriptor);
        static auto unpack_from(adesso::vefs::FileDescriptor &descriptor)
            -> root_sector_info;

        sector_reference root;
        std::uint64_t maximum_extent;
        int tree_depth;
    };
} // namespace vefs::detail
