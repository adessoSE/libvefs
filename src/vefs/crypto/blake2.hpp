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

} // namespace vefs::crypto::detail

// TODO: fixme
template <>
struct SYSTEM_ERROR2_NAMESPACE::quick_status_code_from_enum<
        vefs::crypto::detail::blake2_errc>
    : quick_status_code_from_enum_defaults<vefs::crypto::detail::blake2_errc>
{
    static constexpr auto domain_name = "blake2 domain";
    static constexpr auto domain_uuid = "A6CE508B-5884-4EB7-AB2B-92EBFC4FBE47";

    static auto value_mappings() -> std::initializer_list<mapping> const &
    {
        using blake2_errc = vefs::crypto::detail::blake2_errc;

        static const std::initializer_list<mapping> v = {
                {         blake2_errc::finalization_failed,
                 "the blake2 finalization call failed",                {errc::unknown}                      },
                {         blake2_errc::invalid_digest_size,
                 "the requested digest size was to big",       {errc::invalid_argument}                     },
                {            blake2_errc::invalid_key_size,
                 "the given key blob doesn't is either missing or oversized",       {errc::invalid_argument}},
                {blake2_errc::invalid_personalization_size,
                 "the given personalization blob is too long or missing", {errc::function_not_supported}    },
                {           blake2_errc::state_init_failed,
                 "the state init api call failed",                {errc::unknown}                           },
                {     blake2_errc::state_init_w_key_failed,
                 "the state init with key api call failed",                {errc::unknown}                  },
                {     blake2_errc::state_init_param_failed,
                 "the state init with param api call failed",                {errc::unknown}                },
                {               blake2_errc::update_failed,
                 "the update call failed",                {errc::unknown}                                   },
        };
        return v;
    }
};

namespace vefs::crypto::detail
{

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

inline constexpr std::array<std::byte, blake2b::personal_bytes>
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
inline constexpr std::span vefs_blake2b_personalization_view{
        vefs_blake2b_personalization};
} // namespace vefs::crypto::detail
