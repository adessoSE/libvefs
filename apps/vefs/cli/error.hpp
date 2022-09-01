#pragma once

#include <vefs/disappointment/error.hpp>

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
     * @brief Failed to decode a base64 payload
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

error_domain const &cli_domain() noexcept;

inline auto make_error(cli_errc errc, adl::disappointment::type) noexcept
        -> error
{
    return {static_cast<error_code>(errc), cli_domain()};
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
