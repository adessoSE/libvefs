#pragma once

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/secure_array.hpp>

#include "../crypto/counter.hpp"
#include "../crypto/provider.hpp"

namespace vefs::detail
{
    class file_crypto_ctx
    {
    public:
        // #TODO extract sector_device constants
        auto seal_sector(rw_blob<1 << 15> ciphertext, rw_blob<16> mac,
                         crypto::crypto_provider &provider, ro_blob<16> sessionSalt,
                         ro_blob<(1 << 15) - (1 << 5)> data) noexcept -> result<void>;
        auto unseal_sector(rw_blob<(1 << 15) - (1 << 5)> data, crypto::crypto_provider &provider,
                           ro_blob<1 << 15> ciphertext, ro_blob<16> mac) const noexcept
            -> result<void>;

    private:
        utils::secure_byte_array<32> secret;
        std::atomic<crypto::counter> write_counter;
    };
} // namespace vefs::detail
