#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <lyra/lyra.hpp>

#include <vefs/archive.hpp>

#include "vefs/cli/error.hpp"

// fmt 9.0.0
#if FMT_VERSION >= 9'00'00
template <>
struct fmt::formatter<lyra::parser> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<lyra::group> : fmt::ostream_formatter
{
};
#endif

namespace vefs::cli
{

struct storage_key
{
    std::array<std::byte, archive_handle::key_size> bytes;
};

struct archive_options
{
public:
    std::string path{};

    std::optional<std::string> key{std::nullopt};

    bool mdcProvider{false};
    std::string mdcPassword{};

    template <typename Parser>
    explicit archive_options(Parser &cmd)
    {
        using namespace std::string_literals;

        auto archivePathHelp = "The relative or absolute path to the archive"s;
        cmd.add_argument(
                lyra::opt(path, "archive-path")["-f"]["--file"].required().help(
                        archivePathHelp));

        auto rawKeyHelp
                = "The base64 encoded archive key. The user is prompted for "
                  "the password if omitted. Only one of '--key' and "
                  "'--password' may be supplied."s;
        cmd.add_argument(lyra::opt(key, "base64-key")["--key"].optional().help(
                rawKeyHelp));

        auto mdcProviderHelp = "activate MDC key provider"s;
        auto mdcPasswordHelp
                = "The password for the archive. The user is prompted for the "
                  "password if omitted. Only one of '--key' and '--password' "
                  "may be supplied."s;
        cmd.add_argument(
                lyra::group()
                        .optional()
                        .add_argument(
                                lyra::opt(mdcProvider)["--mdc"].required().help(
                                        mdcProviderHelp))
                        .add_argument(lyra::opt(mdcPassword, "pw")["--password"]
                                              .optional()
                                              .help(mdcPasswordHelp)));
    }

    auto get_key() const noexcept -> result<storage_key>;

    /**
     * @brief Decrypt the storage key of the archive with the given password.
     * The storage key can then be used to decrypt the archive.
     *
     * @param archivePath the path to the archive file
     * @param userPassword the plain text password
     * @return the storage key of the archive or an error
     */
    static auto extract_storage_key(llfio::path_view archivePath,
                                    std::string_view userPassword) noexcept
            -> result<storage_key>;
};

// clang-format off
template <typename T>
concept commandlet
    = std::constructible_from<T, lyra::cli &>
    && requires(T &&t, lyra::group const &g)
    {
        lyra::command(std::string(T::name), nullptr);
        { t.exec(g) } -> std::same_as<result<void>>;
    };
// clang-format on

template <typename T>
struct commandlet_base
{
protected:
    lyra::command cmd;

    commandlet_base()
        : cmd(std::string(T::name),
              [self = static_cast<T *>(this)](lyra::group const &g)
              {
                  if (result<void> rx = self->exec(g); rx.has_failure())
                  {
                      fmt::print("Command execution failed: {}\n{}\n",
                                 rx.assume_error().message().c_str(), g);

                      cli_code{cli_errc::exit_error}.throw_exception();
                  }
              })
    {
    }
};

} // namespace vefs::cli
