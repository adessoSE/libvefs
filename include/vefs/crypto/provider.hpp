#pragma once

#include <type_traits>

#include <vefs/blob.hpp>
#include <vefs/disappointment.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto
{
    namespace detail
    {
        void enable_debug_provider();
    }

    class crypto_provider
    {
    public:
        virtual result<void> box_seal(blob ciphertext, blob mac, blob_view keyMaterial,
            blob_view plaintext) const noexcept = 0;
        virtual result<void> box_open(blob plaintext, blob_view keyMaterial, blob_view ciphertext,
            blob_view mac) const noexcept = 0;

        virtual result<void> random_bytes(blob out) const noexcept = 0;
        virtual utils::secure_byte_array<16> generate_session_salt() const = 0;

        virtual result<int> ct_compare(blob_view l, blob_view r) const noexcept = 0;

        const std::size_t key_material_size;

    protected:
        constexpr crypto_provider(std::size_t keyMaterialSize)
            : key_material_size{ keyMaterialSize }
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

    crypto_provider * boringssl_aes_256_gcm_crypto_provider();
    crypto_provider * debug_crypto_provider();
}

