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
    [[nodiscard]] virtual auto box_seal(rw_dynblob ciphertext,
                                        rw_dynblob mac,
                                        ro_dynblob keyMaterial,
                                        ro_dynblob plaintext) const noexcept
            -> result<void>
            = 0;
    [[nodiscard]] virtual auto box_open(rw_dynblob plaintext,
                                        ro_dynblob keyMaterial,
                                        ro_dynblob ciphertext,
                                        ro_dynblob mac) const noexcept
            -> result<void>
            = 0;

    /**
     * calculates cryptographically save random bytes
     */
    [[nodiscard]] virtual auto random_bytes(rw_dynblob out) const noexcept
            -> result<void>
            = 0;
    [[nodiscard]] virtual auto generate_session_salt() const
            -> utils::secure_byte_array<16>
            = 0;

    /**
     * carries out a constant-time compare
     */
    [[nodiscard]] virtual auto ct_compare(ro_dynblob l,
                                          ro_dynblob r) const noexcept
            -> result<int>
            = 0;

    const std::size_t key_material_size;

protected:
    constexpr crypto_provider(std::size_t keyMaterialSize) noexcept
        : key_material_size{keyMaterialSize}
    {
    }
    constexpr crypto_provider() noexcept
        : key_material_size{5}
    {
    }
    constexpr ~crypto_provider() noexcept = default;
};
static_assert(!std::is_default_constructible_v<crypto_provider>);
static_assert(!std::is_copy_constructible_v<crypto_provider>);
static_assert(!std::is_move_constructible_v<crypto_provider>);
static_assert(!std::is_copy_assignable_v<crypto_provider>);
static_assert(!std::is_move_assignable_v<crypto_provider>);
static_assert(!std::is_destructible_v<crypto_provider>);

} // namespace vefs::crypto
