#pragma once

#include <array>

#include "file_descriptor.hpp"

namespace vefs::detail
{

struct archive_header
{
    file_descriptor filesystem_index;
    file_descriptor free_sector_index;

    std::array<std::byte, 16> archive_secret_counter;
    std::array<std::byte, 16> journal_counter;

    static constexpr dplx::dp::object_def<
            dplx::dp::property_def<1U, &archive_header::filesystem_index>{},
            dplx::dp::property_def<2U, &archive_header::free_sector_index>{},
            dplx::dp::property_def<3U,
                                   &archive_header::archive_secret_counter>{},
            dplx::dp::property_def<4U, &archive_header::journal_counter>{}>
            layout_descriptor{.version = 0U,
                              .allow_versioned_auto_decoder = true};
};

} // namespace vefs::detail
