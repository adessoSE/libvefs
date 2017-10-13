#pragma once

#include <random>

#include <vefs/crypto/provider.hpp>
#include <vefs/utils/secure_array.hpp>

#include "boringssl_aead.hpp"
#include "sysrandom.hpp"


namespace vefs::crypto::detail
{
    class boringssl_aes_256_gcm_provider
        : public crypto_provider
    {
        static constexpr auto scheme = boringssl_aead::scheme::aes_256_gcm;

        virtual void box_seal(blob ciphertext, blob mac, blob_view plaintext,
            key_provider_fn keyProvider) const override
        {
            utils::secure_byte_array<44> prkMem;
            blob prk{ prkMem };
            keyProvider(prk);

            boringssl_aead aead{ prk.slice(0, 32), scheme };

            aead.seal(ciphertext, mac, prk.slice(32, 12), plaintext);
        }

        virtual bool box_open(blob plaintext, blob_view ciphertext, blob_view mac,
            key_provider_fn keyProvider) const override
        {
            utils::secure_byte_array<44> prkMem;
            blob prk{ prkMem };
            keyProvider(prk);

            boringssl_aead aead{ prk.slice(0, 32), scheme };

            return aead.open(plaintext, prk.slice(32, 12), ciphertext, mac);
        }

        virtual utils::secure_byte_array<16> generate_session_salt() const override
        {
            using vefs::detail::random_bytes;
            utils::secure_byte_array<16> salt;
            random_bytes(blob{ salt });
            return salt;
        }
    };
}
