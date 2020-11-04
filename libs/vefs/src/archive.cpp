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

#include "detail/proto-helper.hpp"
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
        VEFS_TRY(primitives, sector_device::open(std::move(mfh), cryptoProvider,
                                                 userPRK, createNew));

        std::unique_ptr<archive> arc{new archive(std::move(primitives))};
        if (auto alloc = new (std::nothrow)
                detail::archive_sector_allocator(*arc->mArchive))
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
                    arc->mArchive->archive_header().filesystem_index))
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
                    arc->mArchive->archive_header().filesystem_index))
            {
                arc->mFilesystem = std::move(crx).assume_value();
            }
            else
            {
                arc->mSectorAllocator.reset();
                crx.assume_error() << ed::archive_file{"[archive-index]"};
                return std::move(crx).as_failure();
            }

            auto &freeSectorHeader =
                arc->mArchive->archive_header().free_sector_index;
            if (freeSectorHeader.tree_info.root.sector == sector_id::master)
            {
                VEFS_TRY(arc->mFilesystem->recover_unused_sectors());

                VEFS_TRY_INJECT(arc->mSectorAllocator->initialize_new(),
                                ed::archive_file{"[free-block-list]"});
            }
            else
            {
                VEFS_TRY_INJECT(arc->mSectorAllocator->initialize_from(
                                    freeSectorHeader.tree_info,
                                    freeSectorHeader.crypto_ctx),
                                ed::archive_file{"[free-block-list]"});

                freeSectorHeader.tree_info = {};
                VEFS_TRY(arc->mArchive->update_header());
            }
        }
        return std::move(arc);
    }

    auto archive::validate(llfio::mapped_file_handle mfh,
                           crypto::crypto_provider *cryptoProvider,
                           ro_blob<32> userPRK) -> result<void>
    {
        VEFS_TRY(primitives, sector_device::open(std::move(mfh), cryptoProvider,
                                                 userPRK, false));

        std::unique_ptr<archive> arc{new archive(std::move(primitives))};
        if (auto alloc = new (std::nothrow)
                detail::archive_sector_allocator(*arc->mArchive))
        {
            alloc->on_leak_detected(); // dirty hack to prevent overwriting
                                       // the archive header on destruction
            arc->mSectorAllocator.reset(alloc);
        }
        else
        {
            return errc::not_enough_memory;
        }

        if (auto crx = vfilesystem::open_existing(
                *arc->mArchive, *arc->mSectorAllocator, arc->mWorkTracker,
                arc->mArchive->archive_header().filesystem_index))
        {
            arc->mFilesystem = std::move(crx).assume_value();
            return arc->mFilesystem->validate();
        }
        else
        {
            return std::move(crx.error())
                   << ed::archive_file("[archive-index]");
        }
    }

    archive::~archive()
    {
        if (mSectorAllocator && !mSectorAllocator->sector_leak_detected())
        {
            auto &header = mArchive->archive_header().free_sector_index;
            if (auto rx = mSectorAllocator->serialize_to(header.crypto_ctx))
            {
                header.tree_info = rx.assume_value();
                (void)mArchive->update_header();
            }
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
            readrx.assume_error() << ed::archive_file_read_area{ed::file_span{
                readFilePos, readFilePos + buffer.size()}}
            /*<<
               ed::archive_file{handle.value()->name()}*/
            ;
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
            writerx.assume_error() << ed::archive_file_write_area{ed::file_span{
                writeFilePos, writeFilePos + data.size()}}
            /*<<
               ed::archive_file{handle.value()->name()}*/
            ;
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
        // if (resizerx.has_error())
        //{
        //    resizerx.assume_error() <<
        //    ed::archive_file{handle.value()->name()};
        //}
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

    auto archive::commit(const vfile_handle &handle) -> result<void>
    {
        if (!handle)
        {
            return errc::invalid_argument;
        }

        auto syncrx = handle->commit();
        // if (syncrx.has_error())
        //{
        //    syncrx.assume_error() << ed::archive_file{handle.value()->name()};
        //}
        return syncrx;
    }
} // namespace vefs
