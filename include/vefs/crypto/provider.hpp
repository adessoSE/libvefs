#pragma once

#include <functional>

#include <vefs/blob.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto
{
    namespace detail
    {
        void enable_debug_provider();
    }

    using key_provider_fn = std::function<void(blob)>;

    class crypto_provider
    {
    public:
        virtual ~crypto_provider() = default;

        virtual void box_seal(blob ciphertext, blob mac, blob_view plaintext,
            key_provider_fn keyProvider) const = 0;
        virtual bool box_open(blob plaintext, blob_view ciphertext, blob_view mac,
            key_provider_fn keyProvider) const = 0;

        virtual void random_bytes(blob out) const = 0;
        virtual utils::secure_byte_array<16> generate_session_salt() const = 0;
    };

    crypto_provider * boringssl_aes_256_gcm_crypto_provider();
    crypto_provider * debug_crypto_provider();
}

