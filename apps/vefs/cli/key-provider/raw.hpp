#pragma once

#include <string_view>

#include <vefs/disappointment.hpp>
#include <vefs/llfio.hpp>

#include "vefs/cli/commandlets/base.hpp"

namespace vefs::cli
{

auto raw_derive_key(llfio::path_view, std::string_view b64RawKey)
        -> result<storage_key>;

} // namespace vefs::cli
