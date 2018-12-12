#pragma once

#include <vefs/disappointment/fwd.hpp>

namespace vefs
{
    enum class errc : error_code
    {
        bad,
        invalid_argument,
        key_already_exists,
        not_enough_memory,
        not_supported,
        result_out_of_range,
        user_object_copy_failed,
    };
    auto generic_domain() noexcept
        -> const error_domain &;
}
