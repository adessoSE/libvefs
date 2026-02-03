#pragma once

#include <dplx/cncr/data_defined_status_domain.hpp>
#include <dplx/predef/compiler.h>

#include <status-code/status_code.hpp>

#include <vefs/disappointment.hpp>
#include <vefs/disappointment/fwd.hpp>

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

} // namespace vefs::cli

template <>
struct dplx::cncr::status_enum_definition<vefs::cli::cli_errc>
    : status_enum_definition_defaults<vefs::cli::cli_errc>
{
    static constexpr char domain_id[]
            = "{F62C53F1-F5AC-4732-B3E3-16FC715A89FD}";
    static constexpr char domain_name[] = "vefs-cli-domain";

    static constexpr value_descriptor values[] = {
            // clang-format off
            {               code::exit_error,                generic_errc::unknown, "Terminate the program with return value 1 now."                        },
            {             code::bad_key_size,       generic_errc::invalid_argument, "The storage key used to encrypt the archive must be exactly 32 bytes." },
            {       code::bad_base64_payload,       generic_errc::invalid_argument, "Failed to decode a base64 payload."                                    },
            {    code::malformed_mdc_key_box,       generic_errc::invalid_argument, "The mdc key box couldn't be parsed."                                   },
            { code::unsupported_mdc_key_type, generic_errc::function_not_supported, "The mdc key type is not supported."                                    },
            {           code::wrong_password,                generic_errc::unknown, "The supplied password cannot be used to open the box."                 },
            // clang-format on
    };
};

namespace vefs::cli
{

using cli_code = dplx::cncr::data_defined_status_code<cli_errc>;
using cli_error = system_error::status_error<cli_code::domain_type>;

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
