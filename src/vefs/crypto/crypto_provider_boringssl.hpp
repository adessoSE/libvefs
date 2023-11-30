#pragma once

#include <vefs/crypto/provider.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto::detail
{
class boringssl_aes_256_gcm_provider : public crypto_provider
{
    // see implementation 🙄
    // NOLINTNEXTLINE(bugprone-exception-escape)
    [[nodiscard]] auto box_seal(rw_dynblob ciphertext,
                                rw_dynblob mac,
                                ro_dynblob keyMaterial,
                                ro_dynblob plaintext) const noexcept
            -> result<void> override;

    // see implementation 🙄
    // NOLINTNEXTLINE(bugprone-exception-escape)
    [[nodiscard]] auto box_open(rw_dynblob plaintext,
                                ro_dynblob keyMaterial,
                                ro_dynblob ciphertext,
                                ro_dynblob mac) const noexcept
            -> result<void> override;

    [[nodiscard]] auto generate_session_salt() const
            -> utils::secure_byte_array<16> override;

    [[nodiscard]] auto random_bytes(rw_dynblob out) const noexcept
            -> result<void> override;

    [[nodiscard]] auto ct_compare(ro_dynblob l, ro_dynblob r) const noexcept
            -> result<int> override;

public:
    static constexpr std::size_t key_material_size = 32 + 12;

    constexpr boringssl_aes_256_gcm_provider()
        : crypto_provider(boringssl_aes_256_gcm_provider::key_material_size)
    {
    }
    constexpr virtual ~boringssl_aes_256_gcm_provider() noexcept = default;
};
} // namespace vefs::crypto::detail
