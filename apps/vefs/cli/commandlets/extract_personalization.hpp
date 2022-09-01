#pragma once

#include <string>

#include <lyra/lyra.hpp>

#include "vefs/cli/commandlets/base.hpp"

namespace vefs::cli
{

class extract_personalization : public commandlet_base<extract_personalization>
{
    archive_options &mArchiveOptions;
    std::string mTargetFile;

public:
    extract_personalization(lyra::cli &parser, archive_options &archiveOptions)
        : commandlet_base<extract_personalization>()
        , mArchiveOptions(archiveOptions)
        , mTargetFile()
    {
        cmd.help("Extract the personalization area to a file. If --mdc was "
                 "specified only the json portion will be extracted");
        cmd.add_argument(lyra::opt(mTargetFile, "file")["--to"].required().help(
                "The file path where the archive personalization area"
                "shall be written to."));

        parser |= cmd;
    }
    static constexpr std::string_view name = "extract-personalization";

    auto exec(lyra::group const &g) const -> result<void>;
};

} // namespace vefs::cli
