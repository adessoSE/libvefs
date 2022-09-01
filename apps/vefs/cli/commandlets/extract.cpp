#include "vefs/cli/commandlets/extract.hpp"

namespace vefs::cli
{

auto extract::exec(lyra::group const &) const -> result<void>
{
    VEFS_TRY(auto &&key, mArchiveOptions.get_key());

    auto const cryptoProvider
            = vefs::crypto::boringssl_aes_256_gcm_crypto_provider();

    VEFS_TRY(auto &&archive,
             vefs::archive({}, mArchiveOptions.path, key.bytes, cryptoProvider,
                           vefs::creation::open_existing));

    for (auto const &vFilePath : mFilePaths)
    {
        VEFS_TRY(archive.extract(vFilePath, mTargetDirectory));
    }
    return oc::success();
}

} // namespace vefs::cli
