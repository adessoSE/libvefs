#pragma once

#include <lyra/lyra.hpp>

#include "vefs/cli/commandlets/base.hpp"
#include "vefs/cli/error.hpp"

namespace vefs::cli
{

class upsert : public commandlet_base<upsert>
{
    archive_options &mArchiveOptions;
    std::string mSourceDirectory;
    std::vector<std::string> mFilePaths;

public:
    upsert(lyra::cli &parser, archive_options &archiveOptions)
        : commandlet_base<upsert>()
        , mArchiveOptions(archiveOptions)
    {
        cmd.help("updates or inserts the specified files in an archive");
        cmd.add_argument(
                lyra::opt(mSourceDirectory, "dir")["--from"].optional().help(
                        "The base directory for the file insertions. Defaults "
                        "to the current working directory."));

        cmd.add_argument(lyra::literal("--"));
        cmd.add_argument(lyra::arg(mFilePaths, "v-file").required());

        parser |= cmd;
    }
    static constexpr std::string_view name = "upsert";

    auto exec(lyra::group const &) const -> result<void>;
};

} // namespace vefs::cli
