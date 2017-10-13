#pragma once

#include <vefs/utils/secure_ops.hpp>
#include <vefs/crypto/provider.hpp>

#include "blake2.hpp"

namespace vefs::crypto::detail
{
    class debug_crypto_provider
        : public crypto_provider
    {
        virtual void box_seal(blob ciphertext, blob mac, blob_view plaintext,
            key_provider_fn keyProvider) const override
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

        virtual bool box_open(blob plaintext, blob_view ciphertext, blob_view mac,
            key_provider_fn keyProvider) const override
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

    public:
        utils::secure_byte_array<16> generate_session_salt() const override
        {
            return {};
        }
    };
}
