#include <ranges>
#include <string>
#include <vector>

#include <boost/predef/compiler.h>
#include <boost/predef/os.h>

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <lyra/lyra.hpp>

#include <vefs/archive.hpp>

#include "vefs/cli/commandlets/extract.hpp"
#include "vefs/cli/commandlets/extract_all.hpp"
#include "vefs/cli/commandlets/extract_personalization.hpp"
#include "vefs/cli/commandlets/upsert.hpp"
#include "vefs/cli/commandlets/validate.hpp"

#if defined(BOOST_COMP_GNUC_AVAILABLE)
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#endif

// fmt 9.0.0
#if FMT_VERSION >= 9'00'00
template <>
struct fmt::formatter<lyra::cli> : fmt::ostream_formatter
{
};
#endif

namespace vefs::cli
{

auto main(lyra::args args) -> int
{
    lyra::cli cli;
    bool showHelp = false;

    cli |= lyra::help(showHelp);

    archive_options archiveOptions(cli);

    validate validate(cli, archiveOptions);
    extract_all extractAll(cli, archiveOptions);
    extract extractSpecific(cli, archiveOptions);
    extract_personalization extractPersonalization(cli, archiveOptions);
    upsert upsert(cli, archiveOptions);

    try
    {
        auto parseResult = cli.parse(args);
        if (showHelp || std::ranges::ssize(args) < 2)
        {
            fmt::print("{}\n", cli);
        }
        else if (!parseResult.is_ok())
        {
            fmt::print(stderr, "Failed to parse the cli args: {}\n{}",
                       parseResult.message(), cli);
            return 1;
        }
    }
    catch (error_exception const &exc)
    {
        if (exc.error() == cli_errc::exit_error)
        {
            // error message already printed
            return 1;
        }
        // print nice error message
        fmt::print(stderr, "Command failed unexpectedly: {}\n", exc.error());
        return 1;
    }
    return 0;
}

} // namespace vefs::cli

#if defined(BOOST_OS_WINDOWS_AVAILABLE)

#include <dplx/cncr/windows-proper.h>

namespace
{

auto win32_utf16_to_utf8(std::wstring_view u16str) noexcept -> std::string
{
    constexpr unsigned utf8_codepage = 65001U;

    std::string u8str;

    if (!std::in_range<int>(u16str.size()))
    {
        fmt::print(stderr,
                   "Oversized (> 2^31b) string commandline argument.\n");
        std::exit(0x10);
    }

    auto const outSize = ::WideCharToMultiByte(utf8_codepage, 0, u16str.data(),
                                               static_cast<int>(u16str.size()),
                                               nullptr, 0, nullptr, nullptr);
    if (0 >= outSize)
    {
        std::error_code code(::GetLastError(), std::system_category());
        fmt::print(
                stderr,
                "Failed to convert an argument with WideCharToMultiByte(). \n"
                "Last win32 error code: {:#010x}\n{}\n",
                code.value(), code.message());
        std::exit(0x11);
    }

    u8str.resize(outSize);

    if (0 >= ::WideCharToMultiByte(utf8_codepage, 0, u16str.data(),
                                   static_cast<int>(u16str.size()),
                                   reinterpret_cast<char *>(u8str.data()),
                                   outSize, nullptr, nullptr))
    {
        std::error_code code(::GetLastError(), std::system_category());
        fmt::print(
                stderr,
                "Failed to convert an argument with WideCharToMultiByte(). \n"
                "Last win32 error code: {:#010x}\n{}\n",
                code.value(), code.message());
        std::exit(0x12);
    }

    return u8str;
}

} // namespace

auto wmain(int argc, wchar_t *argv[]) -> int
{
    auto utf8Args = std::span<wchar_t *>(argv, static_cast<std::size_t>(argc))
                  | std::ranges::views::transform(win32_utf16_to_utf8);
    return vefs::cli::main(lyra::args(std::ranges::begin(utf8Args),
                                      std::ranges::end(utf8Args)));
}

#else

auto main(int argc, char *argv[]) -> int
{
    return vefs::cli::main(lyra::args(argc, argv));
}

#endif
