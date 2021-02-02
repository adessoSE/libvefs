#include "libb2_none_blake2b_crypto_provider.hpp"

#include <vefs/platform/secure_memzero.hpp>

#include "../src/crypto/blake2.hpp"
#include "../src/crypto/ct_compare.hpp"
#include "../src/crypto/provider.hpp"

#include <vefs/span.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::test
{
class libb2_none_blake2b_crypto_provider : public vefs::crypto::crypto_provider
{
    result<void> box_seal(rw_dynblob ciphertext,
                          rw_dynblob mac,
                          ro_dynblob keyMaterial,
                          ro_dynblob plaintext) const noexcept override;

    vefs::result<void> box_open(rw_dynblob plaintext,
                                ro_dynblob keyMaterial,
                                ro_dynblob ciphertext,
                                ro_dynblob mac) const noexcept override;

    utils::secure_byte_array<16> generate_session_salt() const override;

    result<void> random_bytes(rw_dynblob out) const noexcept override;

    result<int> ct_compare(ro_dynblob l, ro_dynblob r) const noexcept override;

public:
    static constexpr std::size_t key_material_size
            = vefs::crypto::detail::blake2b::max_key_bytes;

    constexpr libb2_none_blake2b_crypto_provider();
};

constexpr libb2_none_blake2b_crypto_provider::
        libb2_none_blake2b_crypto_provider()
    : crypto_provider(libb2_none_blake2b_crypto_provider::key_material_size)
{
}

vefs::crypto::crypto_provider *only_mac_crypto_provider()
{
    static libb2_none_blake2b_crypto_provider debug_provider;
    return &debug_provider;
}

result<void> libb2_none_blake2b_crypto_provider::box_seal(
        rw_dynblob ciphertext,
        rw_dynblob mac,
        ro_dynblob keyMaterial,
        ro_dynblob plaintext) const noexcept
{
    // TODO: check whether buffers alias

    if (ciphertext.data() != plaintext.data())
    {
        copy(plaintext, ciphertext);
    }

    const auto hashLen
            = std::min(mac.size(), vefs::crypto::detail::blake2b::digest_bytes);
    vefs::crypto::detail::blake2b blakeCtx{};
    VEFS_TRY(blakeCtx.init(
            hashLen, keyMaterial,
            vefs::crypto::detail::vefs_blake2b_personalization_view));

    VEFS_TRY(blakeCtx.update(plaintext));
    VEFS_TRY(blakeCtx.final(mac.subspan(0, hashLen)));

    if (mac.size() > vefs::crypto::detail::blake2b::digest_bytes)
    {
        utils::secure_memzero(
                mac.subspan(vefs::crypto::detail::blake2b::digest_bytes));
    }

    return outcome::success();
}

result<void>
libb2_none_blake2b_crypto_provider::box_open(rw_dynblob plaintext,
                                             ro_dynblob keyMaterial,
                                             ro_dynblob ciphertext,
                                             ro_dynblob mac) const noexcept
{
    // TODO: check whether buffers alias

    const auto hashLen
            = std::min(mac.size(), vefs::crypto::detail::blake2b::digest_bytes);
    vefs::crypto::detail::blake2b blakeCtx{};
    VEFS_TRY(blakeCtx.init(
            hashLen, keyMaterial,
            vefs::crypto::detail::vefs_blake2b_personalization_view));

    VEFS_TRY(blakeCtx.update(ciphertext));

    std::vector<std::byte> cpMacMem{mac.size(), std::byte{}};
    span cpMac{cpMacMem};

    VEFS_TRY(blakeCtx.final(cpMac.subspan(0, hashLen)));

    VEFS_TRY(auto &&cmp, ct_compare(cpMac, mac));
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

vefs::utils::secure_byte_array<16>
libb2_none_blake2b_crypto_provider::generate_session_salt() const
{
    return {};
}

result<void>
libb2_none_blake2b_crypto_provider::random_bytes(rw_dynblob out) const noexcept
{
    utils::secure_memzero(out);
    return outcome::success();
}

result<int>
libb2_none_blake2b_crypto_provider::ct_compare(ro_dynblob l,
                                               ro_dynblob r) const noexcept
{
    return ::vefs::crypto::detail::ct_compare(l, r);
}
} // namespace vefs::test
