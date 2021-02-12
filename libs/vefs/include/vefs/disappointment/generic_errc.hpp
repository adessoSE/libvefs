#pragma once

#include <vefs/disappointment/fwd.hpp>

namespace vefs
{
enum class errc : error_code
{
    bad = 1,
    invalid_argument,
    key_already_exists,
    not_enough_memory,
    not_supported,
    result_out_of_range,
    user_object_copy_failed,
    device_busy,
    still_in_use,
    not_loaded,
    entry_was_disposed,
    no_more_data,
    resource_exhausted,
};
auto generic_domain() noexcept -> const error_domain &;
} // namespace vefs
