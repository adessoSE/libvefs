#include "vefs/cli/commandlets/upsert.hpp"

#include <boost/predef/os.h>
#include <fmt/std.h>

#if defined(BOOST_OS_WINDOWS_AVAILABLE)
#include <llfio/ntkernel-error-category/include/ntkernel-error-category/ntkernel_category.hpp>
#endif

#include <vefs/disappointment.hpp>
#include <vefs/llfio.hpp>

namespace vefs::cli
{

namespace
{

using io_buffer_type
        = std::vector<std::byte, llfio::utils::page_allocator<std::byte>>;

inline auto transfer_to_vfile(llfio::file_handle &file,
                              archive_handle &archive,
                              vfile_handle vfile,
                              io_buffer_type &ioBuffer) noexcept -> result<void>
{
    std::uint64_t written{0};
    for (;;)
    {
        llfio::file_handle::buffer_type requestBuffers[]
                = {std::span(ioBuffer)};
        if (auto readRx = file.read({requestBuffers, written});
            readRx.has_error())
        {
#if defined(BOOST_OS_WINDOWS_AVAILABLE)
            constexpr system_error::win32_code error_handle_eof{0x0000'0026};
            constexpr system_error::nt_code status_end_of_file{
                    static_cast<long>(0xc000'0011U)};
            if (readRx.assume_error() == error_handle_eof
                || readRx.assume_error() == status_end_of_file)
            {
                break;
            }
#endif
            return std::move(readRx).as_failure();
        }
        else if (readRx.bytes_transferred() == 0U)
        {
            break;
        }
        else
        {
            VEFS_TRY(archive.write(vfile, std::span(readRx.assume_value()[0]),
                                   written));
            written += readRx.bytes_transferred();
        }
    }
    VEFS_TRY(archive.truncate(vfile, written));
    return oc::success();
}

} // namespace

auto upsert::exec(lyra::group const &) const -> result<void>
{
    VEFS_TRY(auto &&key, mArchiveOptions.get_key());

    auto const cryptoProvider
            = vefs::crypto::boringssl_aes_256_gcm_crypto_provider();

    VEFS_TRY(auto &&archive,
             vefs::archive({}, mArchiveOptions.path, key.bytes, cryptoProvider,
                           vefs::creation::if_needed));

    std::filesystem::path baseDir(mSourceDirectory);
    if (baseDir.empty())
    {
        baseDir = std::filesystem::current_path();
    }
    else
    {
        baseDir = std::filesystem::weakly_canonical(baseDir);
    }
    VEFS_TRY(auto &&baseHandle, llfio::directory({}, baseDir));

    constexpr auto sectorSize = (1U << 15) - (1U << 5);
    io_buffer_type ioBuffer(sectorSize);

    for (auto const &filePath : mFilePaths)
    {
        std::error_code ec{};
        auto const relPath = std::filesystem::relative(filePath, ec);
        if (relPath.empty() || *relPath.begin() == "..")
        {
            fmt::print(
                    stderr,
                    "'{}' resolves to '{}' which is not contained in --from\n",
                    filePath, relPath);
            return cli_errc::exit_error;
        }

        VEFS_TRY(auto &&file, llfio::file(baseHandle, relPath));

        VEFS_TRY(auto &&vfile,
                 archive.open(filePath, file_open_mode::readwrite
                                                | file_open_mode::create));

        auto transferRx
                = cli::transfer_to_vfile(file, archive, vfile, ioBuffer);
        VEFS_TRY(archive.commit(vfile));
        vfile = {};
        if (!transferRx.has_value())
        {
            fmt::print(stderr, "Failed to transfer '{}'\n", filePath);
            (void)archive.erase(filePath);
            return std::move(transferRx).as_failure();
        }
    }
    VEFS_TRY(archive.commit());

    return oc::success();
}

} // namespace vefs::cli
