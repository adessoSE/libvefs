#include "blake2.hpp"

namespace vefs::crypto::detail
{
result<void> blake2b::init(std::size_t digestSize) noexcept
{
    if (!digestSize || digestSize < 16 || digestSize > block_bytes)
    {
        return blake2_errc::invalid_digest_size;
    }

    if (blake2b_init(&mState, digestSize))
    {
        return blake2_errc::state_init_failed;
    }
    return outcome::success();
}

result<void> blake2b::init(std::size_t digestSize, ro_dynblob key) noexcept
{
    if (!digestSize || digestSize < 16 || digestSize > block_bytes)
    {
        return blake2_errc::invalid_digest_size;
    }
    if (key.empty() || key.size() > max_key_bytes)
    {
        return blake2_errc::invalid_key_size;
    }

    if (blake2b_init_key(&mState, digestSize, key.data(), key.size()))
    {
        return blake2_errc::state_init_w_key_failed;
    }
    return outcome::success();
}

result<void> blake2b::init(std::size_t digestSize,
                           ro_dynblob key,
                           ro_blob<personal_bytes> personalisation) noexcept
{
    if (!digestSize || digestSize < 16 || digestSize > block_bytes)
    {
        return blake2_errc::invalid_digest_size;
    }
    if (key.size() > max_key_bytes)
    {
        return blake2_errc::invalid_key_size;
    }

    blake2b_param param;

    param.digest_length = static_cast<uint8_t>(digestSize);
    param.key_length = static_cast<uint8_t>(key.size());
    param.fanout = 1;
    param.depth = 1;
    param.leaf_length = 0;
    param.node_offset = 0;
    param.xof_length = 0;
    param.node_depth = 0;
    param.inner_length = 0;
    fill_blob(as_writable_bytes(std::span(param.reserved)));
    fill_blob(as_writable_bytes(std::span(param.salt)));
    copy(personalisation, as_writable_bytes(std::span(param.personal)));

    if (blake2b_init_param(&mState, &param))
    {
        return blake2_errc::state_init_param_failed;
    }

    if (!key.empty())
    {
        VEFS_TRY(mac_feed_key(*this, key));
    }

    return outcome::success();
}

result<void> blake2b::update(ro_dynblob data) noexcept
{
    if (blake2b_update(&mState, data.data(), data.size()))
    {
        return blake2_errc::update_failed;
    }
    return outcome::success();
}

result<void> blake2b::final(rw_dynblob digest) noexcept
{
    if (blake2b_final(&mState, digest.data(), digest.size()))
    {
        return blake2_errc::finalization_failed;
    }
    return outcome::success();
}

result<void> blake2xb::init(std::size_t digestSize) noexcept
{
    if (!digestSize || digestSize > variable_digest_length)
    {
        return blake2_errc::invalid_digest_size;
    }

    if (blake2xb_init(&mState, digestSize))
    {
        return blake2_errc::state_init_failed;
    }
    return outcome::success();
}

result<void> blake2xb::init(std::size_t digestSize, ro_dynblob key) noexcept
{
    if (!digestSize || digestSize > variable_digest_length)
    {
        return blake2_errc::invalid_digest_size;
    }
    if (key.empty() || key.size() > max_key_bytes)
    {
        return blake2_errc::invalid_key_size;
    }

    if (blake2xb_init_key(&mState, digestSize, key.data(), key.size()))
    {
        return blake2_errc::state_init_w_key_failed;
    }
    return outcome::success();
}

result<void> blake2xb::init(std::size_t digestSize,
                            ro_dynblob key,
                            ro_blob<personal_bytes> personalisation) noexcept
{
    if (!digestSize || digestSize > variable_digest_length)
    {
        return blake2_errc::invalid_digest_size;
    }
    if (personalisation.empty())
    {
        return blake2_errc::invalid_personalization_size;
    }
    if (key.size() > max_key_bytes)
    {
        return blake2_errc::invalid_key_size;
    }

    blake2b_param &param = mState.P[0];

    param.digest_length = BLAKE2B_OUTBYTES;
    param.key_length = static_cast<uint8_t>(key.size());
    param.fanout = 1;
    param.depth = 1;
    param.leaf_length = 0;
    param.node_offset = 0;
    param.xof_length = static_cast<uint32_t>(digestSize);
    param.node_depth = 0;
    param.inner_length = 0;
    fill_blob(as_writable_bytes(std::span(param.reserved)));
    fill_blob(as_writable_bytes(std::span(param.salt)));
    copy(personalisation, as_writable_bytes(std::span(param.personal)));

    if (blake2b_init_param(mState.S, &param))
    {
        return blake2_errc::state_init_param_failed;
    }

    if (!key.empty())
    {
        VEFS_TRY(mac_feed_key(*this, key));
    }

    return outcome::success();
}

result<void> blake2xb::update(ro_dynblob data) noexcept
{
    if (blake2xb_update(&mState, data.data(), data.size()))
    {
        return blake2_errc::update_failed;
    }
    return outcome::success();
}

result<void> blake2xb::final(rw_dynblob digest) noexcept
{
    if (blake2xb_final(&mState, digest.data(), digest.size()))
    {
        return blake2_errc::finalization_failed;
    }
    return outcome::success();
}

namespace
{
class blake2_error_domain_type : public error_domain
{
public:
    constexpr blake2_error_domain_type() noexcept = default;

    auto name() const noexcept -> std::string_view override;
    auto message(const error &e, const error_code code) const noexcept
            -> std::string_view override;
};
constexpr blake2_error_domain_type blake2_error_domain_v{};

auto blake2_error_domain_type::name() const noexcept -> std::string_view
{
    using namespace std::string_view_literals;
    return "libb2-error-domain"sv;
}
auto blake2_error_domain_type::message(const error &,
                                       const error_code code) const noexcept
        -> std::string_view
{
    using namespace std::string_view_literals;

    const auto value = static_cast<blake2_errc>(code);
    switch (value)
    {
    case blake2_errc::finalization_failed:
        return "the blake2 finalization call failed"sv;

    case blake2_errc::invalid_digest_size:
        return "the requested digest size was to big"sv;

    case blake2_errc::invalid_key_size:
        return "the given key blob doesn't is either missing or oversized"sv;

    case blake2_errc::invalid_personalization_size:
        return "the given personalization blob is too long or missing"sv;

    case blake2_errc::state_init_failed:
        return "the state init api call failed"sv;

    case blake2_errc::state_init_w_key_failed:
        return "the state init with key api call failed"sv;

    case blake2_errc::state_init_param_failed:
        return "the state init with param api call failed"sv;

    case blake2_errc::update_failed:
        return "the update call failed"sv;

    default:
        return "unknown value"sv;
    }
}
} // namespace

auto blake2_error_domain() noexcept -> const error_domain &
{
    return blake2_error_domain_v;
}
} // namespace vefs::crypto::detail
