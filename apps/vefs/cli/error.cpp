#include "error.hpp"

namespace vefs::cli
{
class cli_domain_type final : public error_domain
{
    auto name() const noexcept -> std::string_view override;
    auto message(error const &, const error_code code) const noexcept
            -> std::string_view override;

public:
    constexpr cli_domain_type() noexcept = default;
};

auto cli_domain_type::name() const noexcept -> std::string_view
{
    using namespace std::string_view_literals;

    return "vefs-cli-domain"sv;
}

auto cli_domain_type::message(error const &,
                              const error_code value) const noexcept
        -> std::string_view
{
    using enum cli_errc;
    using namespace std::string_view_literals;

    const cli_errc code{value};

    switch (code)
    {
    case exit_error:
        return "program encountered error"sv;
    case bad_key_size:
        return "The storage key must consist of exactly 32b."sv;
    case bad_base64_payload:
        return "got an invalid base64 input string"sv;
    case malformed_mdc_key_box:
        return "The mdc key box could not be decoded"sv;
    case unsupported_mdc_key_type:
        return "The mdc key box has been encrypted with an unsupported "
               "algorithm"sv;
    case wrong_password:
        return "The supplied password cannot be used to open the box."sv;
    default:
        return "unknown vefs cli error code"sv;
    }
}

namespace
{
constexpr cli_domain_type cli_domain_v{};
}

auto cli_domain() noexcept -> error_domain const &
{
    return cli_domain_v;
}

} // namespace vefs::cli
