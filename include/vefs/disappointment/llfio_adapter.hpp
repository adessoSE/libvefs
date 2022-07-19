#pragma once

#include <type_traits>
#include <utility>

#include <vefs/disappointment/fwd.hpp>
#include <vefs/llfio.hpp>

namespace vefs::adl::disappointment
{
/**
 * converts llfio::error_info to vefs::error
 */
auto make_error(llfio::error_info const &info,
                adl::disappointment::type) noexcept -> error;
} // namespace vefs::adl::disappointment
