#include "vefs/cli/commandlets/base.hpp"

#include <array>
#include <cstddef>
#include <ranges>
#include <vector>

#include "vefs/cli/key-provider/mdc.hpp"
#include "vefs/cli/key-provider/raw.hpp"

namespace vefs::cli
{

auto archive_options::get_key() const noexcept -> result<storage_key>
{
    if (auto const numProviders
        = std::ranges::count(std::array{mdcProvider, key.has_value()}, true);
        numProviders != 1)
    {
        using namespace std::string_view_literals;
        if (numProviders == 0)
        {
            fmt::print(stderr, "You need to specify at least one key provider. "
                               "Valid key providers are --key and --mdc"sv);
        }
        else
        {
            fmt::print(stderr,
                       "You must not specify more than one key provider."sv);
        }
        return cli_errc::exit_error;
    }

    // raw key provider
    if (key.has_value())
    {
        return raw_derive_key(path, key.value());
    }

    // mdc key provider
    if (mdcProvider)
    {
        return mdc_derive_key(path, mdcPassword);
    }

    outcome::detail::make_ub(this);
}

} // namespace vefs::cli
