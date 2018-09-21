#pragma once

#include <vefs/disappointment/error_info.hpp>

namespace vefs
{
    class error_domain;

    enum class archive_errc
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


    inline error_info make_error_info(archive_errc errc) noexcept
    {
        return { static_cast<error_info::value_type>(errc), archive_domain() };
    }
}
