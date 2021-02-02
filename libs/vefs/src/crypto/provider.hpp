#pragma once

#include <type_traits>

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto
{
class crypto_provider
{
public:
    virtual result<void> box_seal(rw_dynblob ciphertext,
                                  rw_dynblob mac,
                                  ro_dynblob keyMaterial,
                                  ro_dynblob plaintext) const noexcept = 0;
    virtual result<void> box_open(rw_dynblob plaintext,
                                  ro_dynblob keyMaterial,
                                  ro_dynblob ciphertext,
                                  ro_dynblob mac) const noexcept = 0;

    /**
     * calculates cryptographically save random bytes
     */
    virtual result<void> random_bytes(rw_dynblob out) const noexcept = 0;
    virtual utils::secure_byte_array<16> generate_session_salt() const = 0;

    /**
     * carries out a constant-time compare
     */
    virtual result<int> ct_compare(ro_dynblob l,
                                   ro_dynblob r) const noexcept = 0;

    const std::size_t key_material_size;

protected:
    constexpr crypto_provider(std::size_t keyMaterialSize)
        : key_material_size{keyMaterialSize}
    {
    }
    constexpr crypto_provider()
        : key_material_size{5}
    {
    }
    virtual ~crypto_provider() = default;
};
static_assert(!std::is_default_constructible_v<crypto_provider>);
static_assert(!std::is_copy_constructible_v<crypto_provider>);
static_assert(!std::is_move_constructible_v<crypto_provider>);
static_assert(!std::is_copy_assignable_v<crypto_provider>);
static_assert(!std::is_move_assignable_v<crypto_provider>);
static_assert(!std::is_destructible_v<crypto_provider>);

crypto_provider *boringssl_aes_256_gcm_crypto_provider();
} // namespace vefs::crypto
