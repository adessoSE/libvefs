#pragma once

#include <string>
#include <string_view>

#include <lyra/lyra.hpp>

#include "vefs/cli/commandlets/base.hpp"
#include "vefs/cli/error.hpp"

namespace vefs::cli
{

class extract_all : public commandlet_base<extract_all>
{
    archive_options &mArchiveOptions;
    std::string mTargetDirectory;

public:
    extract_all(lyra::cli &parser, archive_options &archiveOptions)
        : commandlet_base<extract_all>()
        , mArchiveOptions(archiveOptions)
        , mTargetDirectory()
    {
        cmd.help("extract all files from an archive");
        cmd.add_argument(
                lyra::opt(mTargetDirectory, "dir")["--to"].required().help(
                        "The directory where the archive is extracted "
                        "to. Must exist beforehand."));

        parser |= cmd;
    }
    static constexpr std::string_view name = "extract-all";

    auto exec(lyra::group const &) const -> result<void>;
};

} // namespace vefs::cli
