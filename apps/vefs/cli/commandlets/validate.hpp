#pragma once

#include <lyra/lyra.hpp>

#include "vefs/cli/commandlets/base.hpp"
#include "vefs/cli/error.hpp"

namespace vefs::cli
{

class validate : public commandlet_base<validate>
{
    archive_options &mArchiveOptions;

public:
    explicit validate(lyra::cli &parser, archive_options &archiveOptions)
        : commandlet_base<validate>()
        , mArchiveOptions(archiveOptions)
    {
        cmd.help("validate an archive");
        parser |= cmd;
    }
    static constexpr std::string_view name = "validate";

    auto exec(lyra::group const &) const -> result<void>;
};

} // namespace vefs::cli
