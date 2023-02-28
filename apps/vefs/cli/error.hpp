#pragma once

#include <dplx/predef/compiler.h>

#include <vefs/disappointment.hpp>

#if defined(DPLX_COMP_GNUC_AVAILABLE)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif

namespace vefs::cli
{
/**
 * @brief CLI specific errors.
 */
enum class cli_errc : error_code
{
    /**
     * @brief Terminate the program with return value 1 now. Error message
     * should be printed before returning this error.
     */
    exit_error,
    /**
     * @brief The storage key used to encrypt the archive must be exactly 32
     * bytes.
     */
    bad_key_size,
    /**
     * @brief Failed to decode a base64 payload.
     */
    bad_base64_payload,
    /**
     * @brief The mdc key box couldn't be parsed.
     */
    malformed_mdc_key_box,
    /**
     * @brief The mdc key type is not supported.
     */
    unsupported_mdc_key_type,
    /**
     * The supplied password cannot be used to open the box.
     */
    wrong_password,
};

class cli_domain_type;
using cli_code = system_error::status_code<cli_domain_type>;
using cli_error = system_error::status_error<cli_domain_type>;

class cli_domain_type : public system_error::status_code_domain
{
    using base = system_error::status_code_domain;
    template <class DomainType>
    friend class system_error::status_code;
    template <class StatusCode>
    friend class system_error::detail::indirecting_domain;

public:
    static constexpr std::string_view uuid
            = "F62C53F1-F5AC-4732-B3E3-16FC715A89FD";

    constexpr ~cli_domain_type() noexcept = default;
    constexpr cli_domain_type() noexcept
        : base(uuid.data(), base::_uuid_size<uuid.size()>{})
    {
    }
    constexpr cli_domain_type(cli_domain_type const &) noexcept = default;
    constexpr auto operator=(cli_domain_type const &) noexcept
            -> cli_domain_type & = default;

    using value_type = cli_errc;
    using base::string_ref;

    constexpr virtual auto name() const noexcept -> string_ref override
    {
        return string_ref("cli-domain");
    }
    constexpr virtual auto payload_info() const noexcept
            -> payload_info_t override
    {
        return {sizeof(value_type),
                sizeof(value_type) + sizeof(cli_domain_type *),
                std::max(alignof(value_type), alignof(cli_domain_type *))};
    }

    static constexpr auto get() noexcept -> cli_domain_type const &;

protected:
    constexpr virtual auto
    _do_failure(system_error::status_code<void> const &code) const noexcept
            -> bool override
    {
        (void)code;
        return true;
    }

    constexpr auto map_to_generic(value_type const value) const noexcept
            -> system_error::errc
    {
        using enum cli_errc;
        using sys_errc = system_error::errc;
        switch (value)
        {
        case exit_error:
            return sys_errc::unknown;
        case bad_key_size:
        case bad_base64_payload:
        case malformed_mdc_key_box:
            return sys_errc::invalid_argument;
        case unsupported_mdc_key_type:
            return sys_errc::function_not_supported;
        case wrong_password:
            return sys_errc::unknown;

        default:
            return sys_errc::unknown;
        }
    }

    constexpr auto map_to_message(value_type const value) const noexcept
            -> std::string_view
    {
        using enum cli_errc;
        using namespace std::string_view_literals;

        switch (value)
        {
        case exit_error:
            return "Terminate the program with return value 1 now."sv;
        case bad_key_size:
            return "The storage key used to encrypt the archive must be exactly 32 bytes."sv;
        case bad_base64_payload:
            return "Failed to decode a base64 payload"sv;
        case malformed_mdc_key_box:
            return "The mdc key box couldn't be parsed."sv;
        case unsupported_mdc_key_type:
            return "The mdc key type is not supported.";
        case wrong_password:
            return "The supplied password cannot be used to open the box.";

        default:
            return "unknown vefs cli error code"sv;
        }
    }

    constexpr virtual auto
    _do_equivalent(system_error::status_code<void> const &lhs,
                   system_error::status_code<void> const &rhs) const noexcept
            -> bool override
    {
        auto const &alhs = static_cast<cli_code const &>(lhs);
        if (rhs.domain() == *this)
        {
            return alhs.value() == static_cast<cli_code const &>(rhs).value();
        }
        else if (rhs.domain() == system_error::generic_code_domain)
        {
            system_error::errc sysErrc
                    = static_cast<system_error::generic_code const &>(rhs)
                              .value();

            return system_error::errc::unknown != sysErrc
                && map_to_generic(alhs.value()) == sysErrc;
        }
        return false;
    }
    constexpr virtual auto
    _generic_code(system_error::status_code<void> const &code) const noexcept
            -> system_error::generic_code override
    {
        return map_to_generic(static_cast<cli_code const &>(code).value());
    }

    constexpr virtual auto
    _do_message(system_error::status_code<void> const &code) const noexcept
            -> string_ref override
    {
        auto const archiveCode = static_cast<cli_code const &>(code);
        auto const message = map_to_message(archiveCode.value());
        return string_ref(message.data(), message.size());
    }

    SYSTEM_ERROR2_NORETURN virtual void _do_throw_exception(
            system_error::status_code<void> const &code) const override
    {
        throw system_error::status_error<cli_domain_type>(
                static_cast<cli_code const &>(code).clone());
    }
};
inline constexpr cli_domain_type cli_domain{};

constexpr auto cli_domain_type::get() noexcept -> cli_domain_type const &
{
    return cli_domain;
}

constexpr auto make_status_code(cli_errc c) noexcept -> cli_code
{
    return cli_code{system_error::in_place, c};
}

namespace
{
/**
 * @brief Error tag used in the CLI to provide more error details.
 */
class cli_error_tag
{
};
} // namespace

/**
 * @brief Use to provide more error details
 *
 * Example:
 * @code
 * cli_errc::exit << cli_error_detail{"my message"};
 * @endcode
 */
using cli_error_detail = error_detail<cli_error_tag, std::string>;

} // namespace vefs::cli

#if defined(DPLX_COMP_GNUC_AVAILABLE)
#pragma GCC diagnostic pop
#endif
