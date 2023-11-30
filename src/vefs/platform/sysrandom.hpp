#pragma once

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>

namespace vefs::detail
{
auto random_bytes(rw_dynblob buffer) noexcept -> result<void>;
}
