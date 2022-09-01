#include "vefs/cli/key-provider/raw.hpp"

#include "vefs/cli/error.hpp"
#include "vefs/cli/utils.hpp"

namespace vefs::cli
{

auto raw_derive_key(llfio::path_view, std::string_view b64RawKey)
        -> result<storage_key>
{
    VEFS_TRY(auto decoded, base64_decode(b64RawKey));
    if (decoded.size() != archive_handle::key_size)
    {
        return cli_errc::bad_key_size;
    }
    storage_key xkey{};
    std::memcpy(xkey.bytes.data(), decoded.data(), xkey.bytes.size());
    return xkey;
}

} // namespace vefs::cli
