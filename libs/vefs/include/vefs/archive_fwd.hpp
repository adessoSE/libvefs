#pragma once

namespace vefs
{
    class file;
    class filesystem;

    class archive;

    namespace detail
    {
        class thread_pool;

        class sector_device;
        struct basic_archive_file_meta;
    }

    namespace crypto
    {
        class crypto_provider;

        // mirroring <vefs/crypto/provider.hpp>
        crypto_provider * boringssl_aes_256_gcm_crypto_provider();
    }
}
