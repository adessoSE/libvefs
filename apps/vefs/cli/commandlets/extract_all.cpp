#include "vefs/cli/commandlets/extract_all.hpp"

namespace vefs::cli
{

auto extract_all::exec(lyra::group const &) const -> result<void>
{
    VEFS_TRY(auto &&key, mArchiveOptions.get_key());

    auto const cryptoProvider
            = vefs::crypto::boringssl_aes_256_gcm_crypto_provider();

    VEFS_TRY(auto &&archive,
             vefs::archive({}, mArchiveOptions.path, key.bytes, cryptoProvider,
                           vefs::creation::open_existing));
    return archive.extractAll(mTargetDirectory);
}

} // namespace vefs::cli
