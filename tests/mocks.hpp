#pragma once
#include "vefs/crypto/provider.hpp"
#include "vefs/detail/file_crypto_ctx.hpp"
#include "gmock/gmock.h"

class crypto_provider_mock : public vefs::crypto::crypto_provider
{
public:
    MOCK_METHOD(vefs::result<void>,
                box_seal,
                (vefs::rw_dynblob ciphertext,
                 vefs::rw_dynblob mac,
                 vefs::ro_dynblob keyMaterial,
                 vefs::ro_dynblob plaintext),
                (const, noexcept));
    MOCK_METHOD(vefs::result<void>,
                box_open,
                (vefs::rw_dynblob plaintext,
                 vefs::ro_dynblob keyMaterial,
                 vefs::ro_dynblob ciphertext,
                 vefs::ro_dynblob mac),
                (const, noexcept));
    MOCK_METHOD(vefs::result<void>,
                random_bytes,
                (vefs::rw_dynblob out),
                (const, noexcept));
    MOCK_METHOD(vefs::utils::secure_byte_array<16>,
                generate_session_salt,
                (),
                (const));
    MOCK_METHOD(vefs::result<int>,
                ct_compare,
                (vefs::ro_dynblob l, vefs::ro_dynblob r),
                (const, noexcept));
};

class file_crypto_ctx_mock : public vefs::detail::file_crypto_ctx_interface
{
public:
    MOCK_METHOD(vefs::result<void>,
                seal_sector,
                (vefs::rw_blob<1 << 15> ciphertext,
                 vefs::rw_blob<16> mac,
                 vefs::crypto::crypto_provider &provider,
                 vefs::ro_blob<16> sessionSalt,
                 vefs::ro_blob<(1 << 15) - (1 << 5)> data),
                (noexcept, override));
    MOCK_METHOD(vefs::result<void>,
                unseal_sector,
                (vefs::rw_blob<(1 << 15) - (1 << 5)> data,
                 vefs::crypto::crypto_provider &provider,
                 vefs::ro_blob<1 << 15> ciphertext,
                 vefs::ro_blob<16> mac),
                (const, noexcept, override));
};
