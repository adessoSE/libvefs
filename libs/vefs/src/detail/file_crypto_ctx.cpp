#include "file_crypto_ctx.hpp"

#include "../crypto/kdf.hpp"

namespace vefs::detail
{
    namespace
    {
        template <std::size_t N>
        constexpr auto byte_literal(const char8_t (&arr)[N]) noexcept
        {
            return span<const char8_t, N>(arr).template first<N - 1>();
        }

        constexpr auto sector_kdf_salt =
            byte_literal(u8"vefs/salt/Sector-Salt");
        constexpr auto sector_kdf_erase = byte_literal(u8"vefs/erase/Sector");
        constexpr auto sector_kdf_prk = byte_literal(u8"vefs/prk/SectorPRK");
    } // namespace

    file_crypto_ctx::file_crypto_ctx(zero_init_t)
        : mState{}
        , mStateSync()
    {
    }

    file_crypto_ctx::file_crypto_ctx(ro_blob<32> secretView,
                                     crypto::counter secretCounter)
        : mState{.counter = secretCounter}
        , mStateSync()
    {
        vefs::copy(secretView, span(mState.secret));
    }

    auto file_crypto_ctx::seal_sector(
        rw_blob<1 << 15> ciphertext, rw_blob<16> mac,
        crypto::crypto_provider &provider, ro_blob<16> sessionSalt,
        ro_blob<(1 << 15) - (1 << 5)> data) noexcept -> result<void>
    {
        utils::secure_byte_array<44> sectorKeyNonce;
        {
            std::lock_guard stateLock{mStateSync};

            // #TODO constant extraction
            auto const salt = ciphertext.first<32>();
            auto const nonce = mState.counter.value();
            mState.counter.increment();
            VEFS_TRY(crypto::kdf(salt, as_bytes(as_span(nonce)),
                                 as_bytes(sector_kdf_salt), sessionSalt));

            VEFS_TRY(crypto::kdf(as_span(sectorKeyNonce), as_span(mState.secret),
                                 as_bytes(sector_kdf_prk), salt));
        }

        return provider.box_seal(ciphertext.subspan<32>(), mac,
                                 as_span(sectorKeyNonce), data);
    }
    auto file_crypto_ctx::unseal_sector(rw_blob<(1 << 15) - (1 << 5)> data,
                                        crypto::crypto_provider &provider,
                                        ro_blob<1 << 15> ciphertext,
                                        ro_blob<16> mac) const noexcept
        -> result<void>
    {
        // #TODO constant extraction
        const auto salt = ciphertext.first<32>();

        utils::secure_byte_array<44> sectorKeyNonce;
        VEFS_TRY(crypto::kdf(as_span(sectorKeyNonce), as_span(mState.secret),
                             as_bytes(sector_kdf_prk), salt));

        return provider.box_open(data, as_span(sectorKeyNonce),
                                 ciphertext.subspan<32>(), mac);
    }
} // namespace vefs::detail
