#include "crypto_provider_debug.hpp"
#include "provider.hpp"

#include <vefs/platform/secure_memzero.hpp>

#include "blake2.hpp"
#include "ct_compare.hpp"

namespace vefs::crypto::detail
{
    constexpr debug_crypto_provider::debug_crypto_provider()
        : crypto_provider(debug_crypto_provider::key_material_size)
    {
    }

    result<void> debug_crypto_provider::box_seal(rw_dynblob ciphertext, rw_dynblob mac,
                                                 ro_dynblob keyMaterial, ro_dynblob plaintext) const
        noexcept
    {
        // TODO: check whether buffers alias

        if (ciphertext.data() != plaintext.data())
        {
            copy(plaintext, ciphertext);
        }

        const auto hashLen = std::min(mac.size(), blake2b::digest_bytes);
        blake2b blakeCtx{};
        BOOST_OUTCOME_TRY(blakeCtx.init(hashLen, keyMaterial, vefs_blake2b_personalization_view));

        BOOST_OUTCOME_TRY(blakeCtx.update(ciphertext));
        BOOST_OUTCOME_TRY(blakeCtx.final(mac.subspan(0, hashLen)));

        if (mac.size() > blake2b::digest_bytes)
        {
            utils::secure_memzero(mac.subspan(blake2b::digest_bytes));
        }

        return outcome::success();
    }

    result<void> debug_crypto_provider::box_open(rw_dynblob plaintext, ro_dynblob keyMaterial,
                                                 ro_dynblob ciphertext, ro_dynblob mac) const
        noexcept
    {
        // TODO: check whether buffers alias

        const auto hashLen = std::min(mac.size(), blake2b::digest_bytes);
        blake2b blakeCtx{};
        BOOST_OUTCOME_TRY(blakeCtx.init(hashLen, keyMaterial, vefs_blake2b_personalization_view));

        BOOST_OUTCOME_TRY(blakeCtx.update(ciphertext));

        std::vector<std::byte> cpMacMem{mac.size(), std::byte{}};
        span cpMac{cpMacMem};

        BOOST_OUTCOME_TRY(blakeCtx.final(cpMac.subspan(0, hashLen)));

        BOOST_OUTCOME_TRYA(cmp, ct_compare(cpMac, mac));
        if (cmp != 0)
        {
            utils::secure_memzero(plaintext);
            return archive_errc::tag_mismatch;
        }
        else if (ciphertext.data() != plaintext.data())
        {
            copy(ciphertext, plaintext);
        }
        return outcome::success();
    }

    vefs::utils::secure_byte_array<16> debug_crypto_provider::generate_session_salt() const
    {
        return {};
    }

    result<void> debug_crypto_provider::random_bytes(rw_dynblob out) const noexcept
    {
        utils::secure_memzero(out);
        return outcome::success();
    }

    result<int> debug_crypto_provider::ct_compare(ro_dynblob l, ro_dynblob r) const noexcept
    {
        return ::vefs::crypto::detail::ct_compare(l, r);
    }
} // namespace vefs::crypto::detail

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

    crypto_provider *debug_crypto_provider()
    {
        if (!debug_provider_enabled)
        {
            BOOST_THROW_EXCEPTION(logic_error{});
        }
        static detail::debug_crypto_provider debug_provider;
        return &debug_provider;
    }
} // namespace vefs::crypto
