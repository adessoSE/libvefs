#pragma once

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/secure_array.hpp>

#include "../crypto/counter.hpp"
#include "../crypto/provider.hpp"

namespace adesso::vefs
{
    class FileDescriptor;
}

namespace vefs::detail
{
    class file_crypto_ctx_interface{
        public:
            virtual void pack_to(adesso::vefs::FileDescriptor &descriptor) = 0;
            virtual void unpack(adesso::vefs::FileDescriptor &descriptor) = 0;

           virtual auto seal_sector(rw_blob<1 << 15> ciphertext, rw_blob<16> mac,
                             crypto::crypto_provider &provider,
                             ro_blob<16> sessionSalt,
                             ro_blob<(1 << 15) - (1 << 5)> data) noexcept
            -> result<void> = 0;
            virtual auto unseal_sector(rw_blob<(1 << 15) - (1 << 5)> data,
                               crypto::crypto_provider &provider,
                               ro_blob<1 << 15> ciphertext, ro_blob<16> mac) const
            noexcept -> result<void> = 0;

    };
    class file_crypto_ctx
    {
    public:
        enum class zero_init_t
        {
        };
        static inline constexpr zero_init_t zero_init = zero_init_t{};

        file_crypto_ctx(zero_init_t);
        file_crypto_ctx(ro_blob<32> secretView, crypto::counter secretCounter);

        void pack_to(adesso::vefs::FileDescriptor &descriptor);
        void unpack(adesso::vefs::FileDescriptor &descriptor);
        static auto unpack_from(adesso::vefs::FileDescriptor &descriptor)
            -> file_crypto_ctx;

        // #TODO extract sector_device constants
        auto seal_sector(rw_blob<1 << 15> ciphertext, rw_blob<16> mac,
                         crypto::crypto_provider &provider,
                         ro_blob<16> sessionSalt,
                         ro_blob<(1 << 15) - (1 << 5)> data) noexcept
            -> result<void>;
        auto unseal_sector(rw_blob<(1 << 15) - (1 << 5)> data,
                           crypto::crypto_provider &provider,
                           ro_blob<1 << 15> ciphertext, ro_blob<16> mac) const
            noexcept -> result<void>;

        utils::secure_byte_array<32> secret;
        std::atomic<crypto::counter> write_counter;
    };
} // namespace vefs::detail
