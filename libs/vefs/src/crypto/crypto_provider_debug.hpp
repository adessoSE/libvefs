#include "provider.hpp"

#include <vefs/span.hpp>
#include <vefs/utils/secure_array.hpp>

#include "blake2.hpp"

namespace vefs::crypto::detail
{
    class debug_crypto_provider : public crypto_provider
    {
        result<void> box_seal(rw_dynblob ciphertext, rw_dynblob mac,
                              ro_dynblob keyMaterial,
                              ro_dynblob plaintext) const noexcept override;

        result<void> box_open(rw_dynblob plaintext, ro_dynblob keyMaterial,
                              ro_dynblob ciphertext, ro_dynblob mac) const
            noexcept override;

        utils::secure_byte_array<16> generate_session_salt() const override;

        result<void> random_bytes(rw_dynblob out) const noexcept override;

        result<int> ct_compare(ro_dynblob l, ro_dynblob r) const
            noexcept override;

    public:
        static constexpr std::size_t key_material_size = blake2b::max_key_bytes;

        constexpr debug_crypto_provider();
    };
} // namespace vefs::crypto::detail
