#pragma once

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>

namespace vefs::detail
{
result<void> random_bytes(rw_dynblob buffer) noexcept;
}
