#include <vefs/archive.hpp>

#include <cassert>
#include <cstdint>

#include <array>
#include <chrono>
#include <sstream>
#include <stack>
#include <string_view>
#include <tuple>

#include <boost/dynamic_bitset.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include <vefs/platform/thread_pool.hpp>
#include <vefs/utils/bitset_overlay.hpp>
#include <vefs/utils/misc.hpp>

#include "detail/sector_device.hpp"
#include "detail/tree_walker.hpp"

#include "detail/archive_sector_allocator.hpp"
#include "vfilesystem.hpp"

using namespace std::string_view_literals;

using namespace vefs::detail;

namespace vefs
{

archive_handle::~archive_handle()
{
    if (mFilesystem)
    {
        if (mSectorAllocator && !mSectorAllocator->sector_leak_detected())
        {
            (void)mSectorAllocator->finalize(mFilesystem->crypto_ctx(),
                                             mFilesystem->committed_root());
        }

        mWorkTracker->wait();
    }
}

archive_handle::archive_handle() noexcept
    : mArchive{}
    , mSectorAllocator{}
    , mWorkTracker{}
    , mFilesystem{}
{
}

archive_handle::archive_handle(sector_device_owner sectorDevice,
                               sector_allocator_owner sectorAllocator,
                               work_tracker_owner workTracker,
                               vfilesystem_owner filesystem) noexcept
    : mArchive{std::move(sectorDevice)}
    , mSectorAllocator{std::move(sectorAllocator)}
    , mWorkTracker{std::move(workTracker)}
    , mFilesystem{std::move(filesystem)}
{
}

archive_handle::archive_handle(archive_handle &&other) noexcept
    : mArchive{std::move(other.mArchive)}
    , mSectorAllocator{std::move(other.mSectorAllocator)}
    , mWorkTracker{std::move(other.mWorkTracker)}
    , mFilesystem{std::move(other.mFilesystem)}
{
}

auto archive_handle::operator=(archive_handle &&other) noexcept
        -> archive_handle &
{
    if (mFilesystem)
    {
        if (mSectorAllocator && !mSectorAllocator->sector_leak_detected())
        {
            (void)mSectorAllocator->finalize(mFilesystem->crypto_ctx(),
                                             mFilesystem->committed_root());
        }

        mWorkTracker->wait();
    }

    mFilesystem = std::move(other.mFilesystem);
    mWorkTracker = std::move(other.mWorkTracker);
    mSectorAllocator = std::move(other.mSectorAllocator);
    mArchive = std::move(other.mArchive);

    return *this;
}

namespace
{

auto map_creation_flag(archive_handle::creation mode) noexcept
        -> llfio::handle::creation
{
    using mapped = llfio::handle::creation;
    switch (mode)
    {
    default:
    case archive_handle::creation::open_existing:
        return mapped::open_existing;
    case archive_handle::creation::only_if_not_exist:
        return mapped::only_if_not_exist;
    case archive_handle::creation::if_needed:
        return mapped::if_needed;
    case archive_handle::creation::always_new:
        return mapped::always_new;
    }
}

} // namespace

auto archive_handle::archive(llfio::file_handle const &file,
                             ro_blob<32> userPRK,
                             crypto::crypto_provider *cryptoProvider,
                             creation creationMode) -> result<archive_handle>
{
    if (!file.is_valid() || !file.is_writable())
    {
        return errc::invalid_argument;
    }

    VEFS_TRY(auto maxExtent, file.maximum_extent());
    bool const created = maxExtent == 0;

    if (created && creationMode == creation::open_existing)
    {
        return archive_errc::archive_file_did_not_exist;
    }
    if (!created && creationMode == creation::only_if_not_exist)
    {
        return archive_errc::archive_file_already_existed;
    }

    VEFS_TRY(llfio::file_handle clonedHandle, file.reopen());

    llfio::mapped_file_handle mappedFile(std::move(clonedHandle), 0,
                                         llfio::section_handle::flag::none);
    if (!created)
    {
        return archive_handle::open_existing(std::move(mappedFile),
                                             cryptoProvider, userPRK);
    }
    else
    {
        return archive_handle::create_new(std::move(mappedFile), cryptoProvider,
                                          userPRK);
    }
}

auto archive_handle::archive(llfio::path_handle const &base,
                             llfio::path_view path,
                             ro_blob<32> userPRK,
                             crypto::crypto_provider *cryptoProvider,
                             creation creationMode) -> result<archive_handle>
{
    auto mappedCreationMode = map_creation_flag(creationMode);

    llfio::file_handle clonedHandle;
    VEFS_TRY(auto fileHandle,
             llfio::mapped_file(base, path, llfio::handle::mode::write,
                                mappedCreationMode));

    bool created = creationMode != creation::open_existing;
    if (creationMode == creation::if_needed)
    {
        VEFS_TRY(auto maxExtent, fileHandle.maximum_extent());
        created = maxExtent == 0;
    }

    if (!created)
    {
        return archive_handle::open_existing(std::move(fileHandle),
                                             cryptoProvider, userPRK);
    }

    VEFS_TRY(clonedHandle,
             (static_cast<llfio::file_handle &>(fileHandle).reopen()));

    if (auto createRx = archive_handle::create_new(std::move(fileHandle),
                                                   cryptoProvider, userPRK))
    {
        return std::move(createRx).assume_value();
    }
    else
    {
        (void)llfio::unlink(clonedHandle);
        return std::move(createRx).assume_error();
    }
}

auto archive_handle::open_existing(llfio::mapped_file_handle mfh,
                                   crypto::crypto_provider *cryptoProvider,
                                   ro_blob<32> userPRK) noexcept
        -> result<archive_handle>
{
    VEFS_TRY(auto &&bundledPrimitives,
             sector_device::open_existing(std::move(mfh), cryptoProvider,
                                          userPRK));
    auto &&[sectorDevice, filesystemFile, freeSectorFile]
            = std::move(bundledPrimitives);

    VEFS_TRY(auto &&sectorAllocator,
             make_unique_rx<detail::archive_sector_allocator>(
                     *sectorDevice, freeSectorFile.crypto_state));

    VEFS_TRY(auto &&workTracker, make_unique_rx<detail::pooled_work_tracker>(
                                         &detail::thread_pool::shared()));

    vfilesystem_owner filesystem;
    if (auto openFsRx = vfilesystem::open_existing(
                *sectorDevice, *sectorAllocator, *workTracker, filesystemFile))
    {
        filesystem = std::move(openFsRx).assume_value();
    }
    else
    {
        openFsRx.assume_error() << ed::archive_file{"[archive-index]"};
        return std::move(openFsRx).assume_error();
    }

    if (freeSectorFile.tree_info.root.sector == sector_id::master)
    {
        VEFS_TRY(filesystem->recover_unused_sectors());

        VEFS_TRY_INJECT(sectorAllocator->initialize_new(),
                        ed::archive_file{"[free-block-list]"});
    }
    else
    {
        VEFS_TRY_INJECT(
                sectorAllocator->initialize_from(freeSectorFile.tree_info),
                ed::archive_file{"[free-block-list]"});

        freeSectorFile.tree_info = {};
        VEFS_TRY(sectorDevice->update_header(
                filesystem->crypto_ctx(), filesystemFile.tree_info,
                sectorAllocator->crypto_ctx(), freeSectorFile.tree_info));
    }

    return result<archive_handle>(
            std::in_place_type<archive_handle>, std::move(sectorDevice),
            std::move(sectorAllocator), std::move(workTracker),
            std::move(filesystem));
}

auto archive_handle::create_new(llfio::mapped_file_handle mfh,
                                crypto::crypto_provider *cryptoProvider,
                                ro_blob<32> userPRK) noexcept
        -> result<archive_handle>
{
    VEFS_TRY(
            auto &&bundledPrimitives,
            sector_device::create_new(std::move(mfh), cryptoProvider, userPRK));
    auto &&[sectorDevice, filesystemFile, freeSectorFile]
            = std::move(bundledPrimitives);

    VEFS_TRY(auto &&sectorAllocator,
             make_unique_rx<detail::archive_sector_allocator>(
                     *sectorDevice, freeSectorFile.crypto_state));

    VEFS_TRY(auto &&workTracker, make_unique_rx<detail::pooled_work_tracker>(
                                         &detail::thread_pool::shared()));

    VEFS_TRY_INJECT(sectorAllocator->initialize_new(),
                    ed::archive_file{"[free-block-list]"});

    vfilesystem_owner filesystem;
    if (auto crx = vfilesystem::create_new(*sectorDevice, *sectorAllocator,
                                           *workTracker, filesystemFile))
    {
        filesystem = std::move(crx).assume_value();
    }
    else
    {
        crx.assume_error() << ed::archive_file{"[archive-index]"};
        return std::move(crx).as_failure();
    }

    return result<archive_handle>(
            std::in_place_type<archive_handle>, std::move(sectorDevice),
            std::move(sectorAllocator), std::move(workTracker),
            std::move(filesystem));
}

auto archive_handle::validate(llfio::path_handle const &base,
                              llfio::path_view path,
                              ro_blob<32> userPRK,
                              crypto::crypto_provider *cryptoProvider)
        -> result<void>
{
    VEFS_TRY(auto fileHandle,
             llfio::mapped_file(base, path, llfio::handle::mode::read,
                                llfio::handle::creation::open_existing));

    VEFS_TRY(auto &&bundledPrimitives,
             sector_device::open_existing(std::move(fileHandle), cryptoProvider,
                                          userPRK));
    auto &&[sectorDevice, filesystemFile, freeSectorFile]
            = std::move(bundledPrimitives);

    VEFS_TRY(auto &&sectorAllocator,
             make_unique_rx<detail::archive_sector_allocator>(
                     *sectorDevice, freeSectorFile.crypto_state));

    VEFS_TRY(auto &&workTracker, make_unique_rx<detail::pooled_work_tracker>(
                                         &detail::thread_pool::shared()));

    vfilesystem_owner filesystem;
    if (auto openFsRx = vfilesystem::open_existing(
                *sectorDevice, *sectorAllocator, *workTracker, filesystemFile))
    {
        filesystem = std::move(openFsRx).assume_value();
    }
    else
    {
        openFsRx.assume_error() << ed::archive_file{"[archive-index]"};
        return std::move(openFsRx).assume_error();
    }

    return filesystem->validate();
}

auto archive_handle::commit() -> result<void>
{
    return mFilesystem->commit();
}

auto archive_handle::open(const std::string_view filePath,
                          const file_open_mode_bitset mode)
        -> result<vfile_handle>
{
    return mFilesystem->open(filePath, mode);
}

auto archive_handle::query(const std::string_view filePath)
        -> result<file_query_result>
{
    return mFilesystem->query(filePath);
}

auto archive_handle::erase(std::string_view filePath) -> result<void>
{
    return mFilesystem->erase(filePath);
}

auto archive_handle::read(const vfile_handle &handle,
                          rw_dynblob buffer,
                          std::uint64_t readFilePos) -> result<void>
{
    if (!buffer)
    {
        return outcome::success();
    }
    if (!handle)
    {
        return errc::invalid_argument;
    }
    auto readrx = handle->read(buffer, readFilePos);
    if (readrx.has_error())
    {
        readrx.assume_error() << ed::archive_file_read_area{
                ed::file_span{readFilePos, readFilePos + buffer.size()}};
    }
    return readrx;
}

auto archive_handle::write(const vfile_handle &handle,
                           ro_dynblob data,
                           std::uint64_t writeFilePos) -> result<void>
{
    if (!data)
    {
        return outcome::success();
    }
    if (!handle)
    {
        return errc::invalid_argument;
    }

    auto writerx = handle->write(data, writeFilePos);
    if (writerx.has_error())
    {
        writerx.assume_error() << ed::archive_file_write_area{
                ed::file_span{writeFilePos, writeFilePos + data.size()}};
    }
    return writerx;
}

auto archive_handle::truncate(const vfile_handle &handle,
                              std::uint64_t maxExtent) -> result<void>
{
    if (!handle)
    {
        return errc::invalid_argument;
    }

    auto resizerx = handle->truncate(maxExtent);

    return resizerx;
}

auto archive_handle::maximum_extent_of(const vfile_handle &handle)
        -> result<std::uint64_t>
{
    if (!handle)
    {
        return errc::invalid_argument;
    }
    return handle->maximum_extent();
}

auto archive_handle::commit(const vfile_handle &handle) -> result<void>
{
    if (!handle)
    {
        return errc::invalid_argument;
    }

    auto syncrx = handle->commit();

    return syncrx;
}

auto archive_handle::personalization_area() noexcept
        -> std::span<std::byte, 1 << 12>
{
    return mArchive->personalization_area();
}

auto archive_handle::sync_personalization_area() noexcept -> result<void>
{
    return mArchive->sync_personalization_area();
}

auto read_archive_personalization_area(
        llfio::path_handle const &base,
        llfio::path_view where,
        std::span<std::byte, 1 << 12> out) noexcept -> result<void>
{
    VEFS_TRY(auto &&file, llfio::file(base, where, llfio::handle::mode::read,
                                      llfio::handle::creation::open_existing));

    return detail::read_archive_personalization_area(file, out);
}
} // namespace vefs
