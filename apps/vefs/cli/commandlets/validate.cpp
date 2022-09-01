#include "vefs/cli/commandlets/validate.hpp"

namespace vefs::cli
{

auto validate::exec(lyra::group const &) const -> result<void>
{
    VEFS_TRY(auto &&key, mArchiveOptions.get_key());

    auto const cryptoProvider
            = vefs::crypto::boringssl_aes_256_gcm_crypto_provider();

    return archive_handle::validate({}, mArchiveOptions.path, key.bytes,
                                    cryptoProvider);
}

} // namespace vefs::cli
