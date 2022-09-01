#include "vefs/cli/utils.hpp"

#include <algorithm>
#include <ranges>
#include <string>

#include <dplx/cncr/math_supplement.hpp>
#include <libbase64.h>

#include "vefs/cli/error.hpp"

namespace vefs::cli
{

static auto base64_decoded_size(std::size_t n) noexcept -> std::size_t
{
    return dplx::cncr::div_ceil(n, 4U) * 3U;
}

auto base64url_decode(std::string_view b64urlEncoded)
        -> result<std::vector<std::byte>>
{
    std::string b64Encoded(b64urlEncoded);
    std::ranges::replace(b64Encoded, '-', '+');
    std::ranges::replace(b64Encoded, '_', '/');
    switch (b64Encoded.size() % 4U)
    {
    case 0U:
    case 1U:
        break;
    case 2U:
        b64Encoded.append(2, '=');
        break;
    case 3U:
        b64Encoded.append(1, '=');
        break;
    }
    return base64_decode(b64Encoded);
}

auto base64_decode(std::string_view b64Encoded)
        -> result<std::vector<std::byte>>
{
    using dplx::cncr::div_ceil;

    std::vector<std::byte> bytes(base64_decoded_size(b64Encoded.size()) + 3U);
    std::size_t outLen = bytes.size();
    if (::base64_decode(b64Encoded.data(), b64Encoded.size(),
                        reinterpret_cast<char *>(bytes.data()), &outLen, 0)
        != 1)
    {
        return cli_errc::bad_base64_payload;
    }
    bytes.resize(outLen);
    return bytes;
}

} // namespace vefs::cli
