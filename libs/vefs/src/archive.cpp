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

#include "archive_file.hpp"
#include "archive_file_lookup.hpp"
#include "detail/archive_free_block_list_file.hpp"
#include "detail/archive_index_file.hpp"
#include "detail/proto-helper.hpp"
#include "detail/sector_device.hpp"
#include "detail/tree_walker.hpp"

using namespace std::string_view_literals;

using namespace vefs::detail;

namespace vefs
{
    archive::file *deref(const archive::file_handle &handle) noexcept
    {
        assert(handle);
        return handle.mData->mWorkingSet.load(std::memory_order_acquire);
    }

    archive::archive()
        : mWorkTracker{&thread_pool::shared()}
    {
    }

    archive::archive(std::unique_ptr<detail::sector_device> primitives)
        : mArchive{std::move(primitives)}
        , mWorkTracker{&thread_pool::shared()}
    {
    }

    auto archive::open(llfio::mapped_file_handle mfh,
                       crypto::crypto_provider *cryptoProvider, ro_blob<32> userPRK,
                       bool createNew) -> result<std::unique_ptr<archive>>
    {
        BOOST_OUTCOME_TRY(primitives,
                          sector_device::open(std::move(mfh), cryptoProvider, userPRK, createNew));

        std::unique_ptr<archive> arc{new archive(std::move(primitives))};

        if (createNew)
        {
            if (auto fblrx = free_block_list_file::create_new(*arc))
            {
                arc->mFreeBlockIndexFile = std::move(fblrx).assume_value();
            }
            else
            {
                fblrx.assume_error() << ed::archive_file{"[free-block-list]"};
                return std::move(fblrx).as_failure();
            }
            if (auto aixrx = index_file::create_new(*arc))
            {
                arc->mArchiveIndexFile = std::move(aixrx).assume_value();
            }
            else
            {
                aixrx.assume_error() << ed::archive_file{"[archive-index]"};
                return std::move(aixrx).as_failure();
            }
        }
        else
        {
            if (auto fblrx = free_block_list_file::open(*arc))
            {
                arc->mFreeBlockIndexFile = std::move(fblrx).assume_value();
            }
            else
            {
                fblrx.assume_error() << ed::archive_file{"[free-block-list]"};
                return std::move(fblrx).as_failure();
            }
            if (auto aixrx = index_file::open(*arc))
            {
                arc->mArchiveIndexFile = std::move(aixrx).assume_value();
            }
            else
            {
                aixrx.assume_error() << ed::archive_file{"[archive-index]"};
                return std::move(aixrx).as_failure();
            }
        }
        return std::move(arc);
    }

    archive::~archive()
    {
        (void)sync();

        mArchiveIndexFile->dispose();
        mArchiveIndexFile = nullptr;
        mFreeBlockIndexFile->dispose();
        mFreeBlockIndexFile = nullptr;
        mWorkTracker.wait();
    }

    auto archive::sync() -> result<void>
    {
        BOOST_OUTCOME_TRY(changes, mArchiveIndexFile->sync(true));
        if (!changes)
        {
            return outcome::success();
        }

        VEFS_TRY_INJECT(mFreeBlockIndexFile->sync(), ed::archive_file{"[free-block-list]"});
        VEFS_TRY_INJECT(mArchive->update_header(), ed::archive_file{"[archive-index]"});
        return outcome::success();
    }

    void archive::sync_async(std::function<void(op_outcome<void>)> cb)
    {
        ops_pool().execute([this, cb = std::move(cb)]() {
            auto einfo = collect_disappointment([&]() { return sync(); });
            cb(std::move(einfo));
        });
    }

    auto archive::open(const std::string_view filePath, const file_open_mode_bitset mode)
        -> result<archive::file_handle>
    {
        return mArchiveIndexFile->open(filePath, mode);
    }

    auto archive::query(const std::string_view filePath) -> result<file_query_result>
    {
        return mArchiveIndexFile->query(filePath);
    }

    auto archive::erase(std::string_view filePath) -> result<void>
    {
        return mArchiveIndexFile->erase(filePath);
    }

    auto archive::read(const file_handle& handle, rw_dynblob buffer, std::uint64_t readFilePos)
        -> result<void>
    {
        if (!buffer)
        {
            return outcome::success();
        }
        if (!handle)
        {
            return errc::invalid_argument;
        }
        auto f = deref(handle);
        if (&f->owner_ref() != this)
        {
            return errc::invalid_argument;
        }
        auto readrx = f->read(buffer, readFilePos);
        if (readrx.has_error())
        {
            readrx.assume_error() << ed::archive_file_read_area{ed::file_span{
                                         readFilePos, readFilePos + buffer.size()}}
                                  << ed::archive_file{handle.value()->name()};
        }
        return readrx;
    }

    auto archive::write(const file_handle& handle, ro_dynblob data, std::uint64_t writeFilePos)
        -> result<void>
    {
        if (!data)
        {
            return outcome::success();
        }
        if (!handle)
        {
            return errc::invalid_argument;
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            return errc::invalid_argument;
        }

        auto writerx = f->write(data, writeFilePos);
        if (writerx.has_error())
        {
            writerx.assume_error() << ed::archive_file_write_area{ed::file_span{
                                          writeFilePos, writeFilePos + data.size()}}
                                   << ed::archive_file{handle.value()->name()};
        }
        return writerx;
    }

    auto archive::resize(const file_handle& handle, std::uint64_t size) -> result<void>
    {
        if (!handle)
        {
            return errc::invalid_argument;
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            return errc::invalid_argument;
        }

        auto resizerx = f->resize(size);
        if (resizerx.has_error())
        {
            resizerx.assume_error() << ed::archive_file{handle.value()->name()};
        }
        return resizerx;
    }

    auto archive::size_of(const file_handle& handle) -> result<std::uint64_t>
    {
        if (!handle)
        {
            return errc::invalid_argument;
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            return errc::invalid_argument;
        }

        return f->size();
    }

    auto archive::sync(const file_handle& handle) -> result<void>
    {
        if (!handle)
        {
            return errc::invalid_argument;
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            return errc::invalid_argument;
        }

        auto syncrx = f->sync();
        if (syncrx.has_error())
        {
            syncrx.assume_error() << ed::archive_file{handle.value()->name()};
        }
        return syncrx;
    }
} // namespace vefs
