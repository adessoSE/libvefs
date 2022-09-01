#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <lyra/lyra.hpp>

#include "vefs/cli/commandlets/base.hpp"
#include "vefs/cli/error.hpp"

namespace vefs::cli
{

class extract : public commandlet_base<extract>
{
    archive_options &mArchiveOptions;
    std::string mTargetDirectory;
    std::vector<std::string> mFilePaths;

public:
    extract(lyra::cli &parser, archive_options &archiveOptions)
        : commandlet_base<extract>()
        , mArchiveOptions(archiveOptions)
        , mTargetDirectory()
    {
        cmd.help("extract specific files from an archive");
        cmd.add_argument(
                lyra::opt(mTargetDirectory, "dir")["--to"].required().help(
                        "The directory where the archive is extracted "
                        "to. Must exist beforehand."));

        cmd.add_argument(lyra::literal("--"));
        cmd.add_argument(lyra::arg(mFilePaths, "v-file").required());

        parser |= cmd;
    }
    static constexpr std::string_view name = "extract";

    auto exec(lyra::group const &) const -> result<void>;
};

} // namespace vefs::cli
