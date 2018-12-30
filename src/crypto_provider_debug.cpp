#pragma once

#include <vefs/utils/secure_ops.hpp>
#include <vefs/crypto/provider.hpp>

#include "blake2.hpp"
#include "ct_compare.hpp"

namespace vefs::crypto::detail
{
    class debug_crypto_provider
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
        static constexpr std::size_t key_material_size = blake2b::max_key_bytes;

        constexpr debug_crypto_provider();
    };

    constexpr debug_crypto_provider::debug_crypto_provider()
        : crypto_provider(debug_crypto_provider::key_material_size)
    {
    }

    result<void> debug_crypto_provider::box_seal(blob ciphertext, blob mac, blob_view keyMaterial,
        blob_view plaintext) const noexcept
    {
        //TODO: check whether buffers alias

        if (ciphertext.data() != plaintext.data())
        {
            plaintext.copy_to(ciphertext);
        }

        blake2b blakeCtx{};
        OUTCOME_TRY(blakeCtx.init(std::min(mac.size(), blake2b::digest_bytes), keyMaterial,
            vefs_blake2b_personalization_view));

        OUTCOME_TRY(blakeCtx.update(ciphertext));
        OUTCOME_TRY(blakeCtx.final(mac.slice(0, blake2b::digest_bytes)));

        if (mac.size() > blake2b::digest_bytes)
        {
            utils::secure_memzero(mac.slice(blake2b::digest_bytes));
        }

        return outcome::success();
    }

    result<void> debug_crypto_provider::box_open(blob plaintext, blob_view keyMaterial,
        blob_view ciphertext, blob_view mac) const noexcept
    {
        //TODO: check whether buffers alias

        blake2b blakeCtx{};
        OUTCOME_TRY(blakeCtx.init(std::min(mac.size(), blake2b::digest_bytes), keyMaterial,
            vefs_blake2b_personalization_view));

        OUTCOME_TRY(blakeCtx.update(ciphertext));

        std::vector<std::byte> cpMacMem{ mac.size(), std::byte{} };
        blob cpMac{ cpMacMem };

        OUTCOME_TRY(blakeCtx.final(cpMac.slice(0, blake2b::digest_bytes)));

        OUTCOME_TRYA(cmp, ct_compare(cpMac, mac));
        if (cmp != 0)
        {
            utils::secure_memzero(plaintext);
            return archive_errc::tag_mismatch;
        }
        else if (ciphertext.data() != plaintext.data())
        {
            ciphertext.copy_to(plaintext);
        }
        return outcome::success();
    }

    vefs::utils::secure_byte_array<16> debug_crypto_provider::generate_session_salt() const
    {
        return {};
    }

    result<void> debug_crypto_provider::random_bytes(blob out) const noexcept
    {
        utils::secure_memzero(out);
        return outcome::success();
    }

    result<int> debug_crypto_provider::ct_compare(blob_view l, blob_view r) const noexcept
    {
        return ::vefs::crypto::detail::ct_compare(l, r);
    }
}

namespace vefs::crypto
{
    namespace
    {
        bool debug_provider_enabled = false;
    }

    void detail::enable_debug_provider()
    {
        debug_provider_enabled = true;
    }

    crypto_provider* debug_crypto_provider()
    {
        if (!debug_provider_enabled)
        {
            BOOST_THROW_EXCEPTION(logic_error{});
        }
        static detail::debug_crypto_provider debug_provider;
        return &debug_provider;
    }
}
