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
    archive::archive(std::unique_ptr<detail::sector_device> primitives)
        : mArchive{std::move(primitives)}
        , mWorkTracker{&thread_pool::shared()}
    {
    }

    auto archive::open(llfio::mapped_file_handle mfh,
                       crypto::crypto_provider *cryptoProvider,
                       ro_blob<32> userPRK, bool createNew)
        -> result<std::unique_ptr<archive>>
    {
        VEFS_TRY(bundledPrimitives,
                 sector_device::open(std::move(mfh), cryptoProvider, userPRK,
                                     createNew));
        auto &&[primitives, filesystemFile, freeSectorFile] =
            std::move(bundledPrimitives);

        std::unique_ptr<archive> arc{new archive(std::move(primitives))};
        if (auto alloc = new (std::nothrow) detail::archive_sector_allocator(
                *arc->mArchive, freeSectorFile.crypto_state))
        {
            arc->mSectorAllocator.reset(alloc);
        }
        else
        {
            return errc::not_enough_memory;
        }

        if (createNew)
        {
            VEFS_TRY_INJECT(arc->mSectorAllocator->initialize_new(),
                            ed::archive_file{"[free-block-list]"});

            if (auto crx = vfilesystem::create_new(
                    *arc->mArchive, *arc->mSectorAllocator, arc->mWorkTracker,
                    filesystemFile))
            {
                arc->mFilesystem = std::move(crx).assume_value();
            }
            else
            {
                crx.assume_error() << ed::archive_file{"[archive-index]"};
                return std::move(crx).as_failure();
            }
        }
        else
        {
            if (auto crx = vfilesystem::open_existing(
                    *arc->mArchive, *arc->mSectorAllocator, arc->mWorkTracker,
                    filesystemFile))
            {
                arc->mFilesystem = std::move(crx).assume_value();
            }
            else
            {
                arc->mSectorAllocator.reset();
                crx.assume_error() << ed::archive_file{"[archive-index]"};
                return std::move(crx).as_failure();
            }

            if (freeSectorFile.tree_info.root.sector == sector_id::master)
            {
                VEFS_TRY(arc->mFilesystem->recover_unused_sectors());

                VEFS_TRY_INJECT(arc->mSectorAllocator->initialize_new(),
                                ed::archive_file{"[free-block-list]"});
            }
            else
            {
                VEFS_TRY_INJECT(arc->mSectorAllocator->initialize_from(
                                    freeSectorFile.tree_info),
                                ed::archive_file{"[free-block-list]"});

                freeSectorFile.tree_info = {};
                VEFS_TRY(arc->mArchive->update_header(
                    arc->mFilesystem->crypto_ctx(), filesystemFile.tree_info,
                    arc->mSectorAllocator->crypto_ctx(),
                    freeSectorFile.tree_info));
            }
        }
        return std::move(arc);
    }

    archive::~archive()
    {
        if (mSectorAllocator && !mSectorAllocator->sector_leak_detected())
        {
            (void)mSectorAllocator->finalize(mFilesystem->crypto_ctx(),
                                             mFilesystem->committed_root());
        }

        mWorkTracker.wait();
    }

    auto archive::commit() -> result<void>
    {
        return mFilesystem->commit();
    }

    auto archive::open(const std::string_view filePath,
                       const file_open_mode_bitset mode) -> result<vfile_handle>
    {
        return mFilesystem->open(filePath, mode);
    }

    auto archive::query(const std::string_view filePath)
        -> result<file_query_result>
    {
        return mFilesystem->query(filePath);
    }

    auto archive::erase(std::string_view filePath) -> result<void>
    {
        return mFilesystem->erase(filePath);
    }

    auto archive::read(const vfile_handle &handle, rw_dynblob buffer,
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

    auto archive::write(const vfile_handle &handle, ro_dynblob data,
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

    auto archive::truncate(const vfile_handle &handle, std::uint64_t maxExtent)
        -> result<void>
    {
        if (!handle)
        {
            return errc::invalid_argument;
        }

        auto resizerx = handle->truncate(maxExtent);

        return resizerx;
    }

    auto archive::maximum_extent_of(const vfile_handle &handle)
        -> result<std::uint64_t>
    {
        if (!handle)
        {
            return errc::invalid_argument;
        }
        return handle->maximum_extent();
    }

    //#Todo Why do we need this method?
    auto archive::commit(const vfile_handle &handle) -> result<void>
    {
        if (!handle)
        {
            return errc::invalid_argument;
        }

        auto syncrx = handle->commit();

        return syncrx;
    }
} // namespace vefs
