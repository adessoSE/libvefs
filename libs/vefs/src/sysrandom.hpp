#pragma once

#include <vefs/blob.hpp>
#include <vefs/disappointment.hpp>

namespace vefs::detail
{
    result<void> random_bytes(blob buffer) noexcept;
}
