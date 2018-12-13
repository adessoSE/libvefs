#pragma once

#include <vefs/disappointment/error.hpp>

namespace vefs
{
    enum class archive_errc : error_code
    {
        invalid_prefix = 1,
        oversized_static_header,
        no_archive_header,
        identical_header_version,
        tag_mismatch,
        invalid_proto,
        incompatible_proto,
        sector_reference_out_of_range,
        corrupt_index_entry,
        free_sector_index_invalid_size,
    };
    const error_domain & archive_domain() noexcept;


    inline auto make_error(archive_errc errc, adl::disappointment::type) noexcept
        -> error
    {
        return { static_cast<error_code>(errc), archive_domain() };
    }
}