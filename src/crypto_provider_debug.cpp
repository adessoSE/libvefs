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
        virtual void box_seal(blob ciphertext, blob mac, blob_view plaintext,
            key_provider_fn keyProvider) const override;

        virtual bool box_open(blob plaintext, blob_view ciphertext, blob_view mac,
            key_provider_fn keyProvider) const override;

        virtual utils::secure_byte_array<16> generate_session_salt() const override;

        virtual void random_bytes(blob out) const override;

        virtual int ct_compare(blob_view l, blob_view r) const override;
    };

    void debug_crypto_provider::box_seal(blob ciphertext, blob mac, blob_view plaintext, key_provider_fn keyProvider) const
    {
        //TODO: check whether buffers alias

        if (ciphertext.data() != plaintext.data())
        {
            plaintext.copy_to(ciphertext);
        }

        utils::secure_byte_array<blake2b::max_key_bytes> keyMem;
        blob key{ keyMem };
        keyProvider(key);

        blake2b blakeCtx{ std::min(mac.size(), blake2b::digest_bytes), key,
            vefs_blake2b_personalization_view };

        blakeCtx.update(ciphertext)
            .final(mac.slice(0, blake2b::digest_bytes));
        if (mac.size() > blake2b::digest_bytes)
        {
            utils::secure_memzero(mac.slice(blake2b::digest_bytes));
        }
    }

    bool debug_crypto_provider::box_open(blob plaintext, blob_view ciphertext, blob_view mac, key_provider_fn keyProvider) const
    {
        //TODO: check whether buffers alias

        utils::secure_byte_array<blake2b::max_key_bytes> keyMem;
        blob key{ keyMem };
        keyProvider(key);

        blake2b blakeCtx{ std::min(mac.size(), blake2b::digest_bytes), key,
            vefs_blake2b_personalization_view };

        std::vector<std::byte> cpMacMem{ mac.size(), std::byte{} };
        blob cpMac{ cpMacMem };

        blakeCtx.update(ciphertext)
            .final(cpMac.slice(0, blake2b::digest_bytes));

        auto success = equal(cpMac, mac);
        if (!success)
        {
            utils::secure_memzero(plaintext);
        }
        else if (ciphertext.data() != plaintext.data())
        {
            ciphertext.copy_to(plaintext);
        }
        return success;
    }

    vefs::utils::secure_byte_array<16> debug_crypto_provider::generate_session_salt() const
    {
        return {};
    }

    void debug_crypto_provider::random_bytes(blob out) const
    {
        utils::secure_memzero(out);
    }

    int debug_crypto_provider::ct_compare(blob_view l, blob_view r) const
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
