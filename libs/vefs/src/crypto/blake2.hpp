#pragma once

#include <array>
#include <stdexcept>

#include <boost/predef.h>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push)
#pragma warning(disable : 4804)
#endif

#include <blake2.h>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif

#include <boost/preprocessor/stringize.hpp>

#include <vefs/disappointment.hpp>
#include <vefs/exceptions.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto::detail
{
enum class blake2_errc
{
    finalization_failed,
    invalid_digest_size,
    invalid_key_size,
    invalid_personalization_size,
    state_init_failed,
    state_init_w_key_failed,
    state_init_param_failed,
    update_failed,
};
auto blake2_error_domain() noexcept -> const error_domain &;

inline auto make_error(blake2_errc value,
                       vefs::adl::disappointment::type) noexcept -> error
{
    return {static_cast<error_code>(value), blake2_error_domain()};
}

template <typename MAC>
result<void> mac_feed_key(MAC &state, ro_dynblob key) noexcept
{
    utils::secure_byte_array<MAC::block_bytes> keyBlockMemory;
    auto keyBlock = as_span(keyBlockMemory);
    copy(key, keyBlock);
    fill_blob(keyBlock.subspan(key.size()));

    return state.update(keyBlock);
}

class blake2b
{
public:
    static constexpr size_t salt_bytes = BLAKE2B_SALTBYTES;
    static constexpr size_t personal_bytes = BLAKE2B_PERSONALBYTES;
    static constexpr size_t digest_bytes = BLAKE2B_OUTBYTES;
    static constexpr size_t block_bytes = BLAKE2B_BLOCKBYTES;
    static constexpr size_t max_key_bytes = BLAKE2B_KEYBYTES;

    constexpr blake2b() noexcept = default;
    ~blake2b() noexcept
    {
        utils::secure_data_erase(mState);
    }

    result<void> init(std::size_t digestSize = digest_bytes) noexcept;
    result<void> init(std::size_t digestSize, ro_dynblob key) noexcept;
    result<void> init(std::size_t digestSize,
                      ro_dynblob key,
                      ro_blob<personal_bytes> personalisation) noexcept;

    result<void> update(ro_dynblob data) noexcept;

    result<void> final(rw_dynblob digest) noexcept;

private:
    blake2b_state mState{};
};

class blake2xb
{
public:
    static constexpr size_t salt_bytes = BLAKE2B_SALTBYTES;
    static constexpr size_t personal_bytes = BLAKE2B_PERSONALBYTES;
    static constexpr size_t block_bytes = BLAKE2B_BLOCKBYTES;
    static constexpr size_t max_key_bytes = BLAKE2B_KEYBYTES;
    static constexpr uint32_t variable_digest_length = 0xFFFF'FFFFu;

    constexpr blake2xb() noexcept = default;
    ~blake2xb() noexcept
    {
        utils::secure_data_erase(mState);
    }

    result<void> init(std::size_t digestSize) noexcept;
    result<void> init(std::size_t digestSize, ro_dynblob key) noexcept;
    result<void> init(std::size_t digestSize,
                      ro_dynblob key,
                      ro_blob<personal_bytes> personalisation) noexcept;

    result<void> update(ro_dynblob data) noexcept;

    result<void> final(rw_dynblob digest) noexcept;

private:
    blake2xb_state mState{};
};

constexpr std::array<std::byte, blake2b::personal_bytes>
        vefs_blake2b_personalization = utils::make_byte_array(0x76,
                                                              0x65,
                                                              0x66,
                                                              0x73,
                                                              0xa4,
                                                              0xa1,
                                                              0x5f,
                                                              0x44,
                                                              0xac,
                                                              0x08,
                                                              0x45,
                                                              0x31,
                                                              0x8d,
                                                              0x08,
                                                              0xd1,
                                                              0x33);
constexpr span vefs_blake2b_personalization_view{vefs_blake2b_personalization};
} // namespace vefs::crypto::detail
