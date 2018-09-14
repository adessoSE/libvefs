#pragma once

#include <random>

#include <vefs/crypto/provider.hpp>
#include <vefs/utils/secure_array.hpp>

#include "boringssl_aead.hpp"
#include "sysrandom.hpp"
#include "ct_compare.hpp"


namespace vefs::crypto::detail
{
    class boringssl_aes_256_gcm_provider
        : public crypto_provider
    {
        static constexpr auto scheme = boringssl_aead::scheme::aes_256_gcm;

        virtual void box_seal(blob ciphertext, blob mac, blob_view plaintext,
            key_provider_fn keyProvider) const override;
        virtual bool box_open(blob plaintext, blob_view ciphertext, blob_view mac,
            key_provider_fn keyProvider) const override;

        virtual utils::secure_byte_array<16> generate_session_salt() const override;
        virtual void random_bytes(blob out) const override;
        virtual int ct_compare(blob_view l, blob_view r) const override;
    };

    void boringssl_aes_256_gcm_provider::box_seal(blob ciphertext, blob mac, blob_view plaintext, key_provider_fn keyProvider) const
    {
        utils::secure_byte_array<44> prkMem;
        blob prk{ prkMem };
        keyProvider(prk);

        boringssl_aead aead{ prk.slice(0, 32), scheme };

        aead.seal(ciphertext, mac, prk.slice(32, 12), plaintext);
    }

    bool boringssl_aes_256_gcm_provider::box_open(blob plaintext, blob_view ciphertext, blob_view mac, key_provider_fn keyProvider) const
    {
        utils::secure_byte_array<44> prkMem;
        blob prk{ prkMem };
        keyProvider(prk);

        boringssl_aead aead{ prk.slice(0, 32), scheme };

        return aead.open(plaintext, prk.slice(32, 12), ciphertext, mac);
    }

    vefs::utils::secure_byte_array<16> boringssl_aes_256_gcm_provider::generate_session_salt() const
    {
        using vefs::detail::random_bytes;
        utils::secure_byte_array<16> salt;
        random_bytes(blob{ salt });
        return salt;
    }

    void boringssl_aes_256_gcm_provider::random_bytes(blob out) const
    {
        vefs::detail::random_bytes(out);
    }

    int boringssl_aes_256_gcm_provider::ct_compare(blob_view l, blob_view r) const
    {
        return ::vefs::crypto::detail::ct_compare(l, r);
    }
}

namespace vefs::crypto
{
    namespace
    {
        detail::boringssl_aes_256_gcm_provider boringssl_aes_256_gcm;
    }

    crypto_provider * boringssl_aes_256_gcm_crypto_provider()
    {
        return &boringssl_aes_256_gcm;
    }
}
