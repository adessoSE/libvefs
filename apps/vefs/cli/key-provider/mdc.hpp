#pragma once

#include <string_view>

#include <vefs/disappointment.hpp>
#include <vefs/llfio.hpp>

#include "vefs/cli/commandlets/base.hpp"

namespace vefs::cli
{

auto mdc_derive_key(llfio::path_view archivePath, std::string_view password)
        -> result<storage_key>;

}
