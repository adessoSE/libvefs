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
        result<void> box_seal(blob ciphertext, blob mac, blob_view keyMaterial,
            blob_view plaintext) const noexcept override;

        result<void> box_open(blob plaintext, blob_view keyMaterial, blob_view ciphertext,
            blob_view mac) const noexcept override;

        utils::secure_byte_array<16> generate_session_salt() const override;

        result<void> random_bytes(blob out) const noexcept override;

        result<int> ct_compare(blob_view l, blob_view r) const noexcept override;

    public:
        static constexpr std::size_t key_material_size = 32 + 12;

        constexpr boringssl_aes_256_gcm_provider()
            : crypto_provider(boringssl_aes_256_gcm_provider::key_material_size)
        {
        }
    };

    result<void> boringssl_aes_256_gcm_provider::box_seal(blob ciphertext, blob mac,
        blob_view keyMaterial, blob_view plaintext) const noexcept
    {
        OUTCOME_TRYA(aead, boringssl_aead::create(keyMaterial.slice(0, 32)));

        return aead.seal(ciphertext, mac, keyMaterial.slice(32, 12), plaintext);
    }

    result<void> boringssl_aes_256_gcm_provider::box_open(blob plaintext, blob_view keyMaterial,
        blob_view ciphertext, blob_view mac) const noexcept
    {
        OUTCOME_TRYA(aead, boringssl_aead::create(keyMaterial.slice(0, 32)));

        return aead.open(plaintext, keyMaterial.slice(32, 12), ciphertext, mac);
    }

    vefs::utils::secure_byte_array<16> boringssl_aes_256_gcm_provider::generate_session_salt() const
    {
        using vefs::detail::random_bytes;
        utils::secure_byte_array<16> salt;
        if (auto r = random_bytes(blob{ salt }); r.has_error())
        {
            throw error_exception{ r.error() };
        }
        return salt;
    }

    result<void> boringssl_aes_256_gcm_provider::random_bytes(blob out) const noexcept
    {
        return vefs::detail::random_bytes(out);
    }

    result<int> boringssl_aes_256_gcm_provider::ct_compare(blob_view l, blob_view r) const noexcept
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
