#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include <vefs/disappointment.hpp>

namespace vefs::cli
{

auto base64_decode(std::string_view b64Encoded)
        -> result<std::vector<std::byte>>;
auto base64url_decode(std::string_view b64urlEncoded)
        -> result<std::vector<std::byte>>;

} // namespace vefs::cli
