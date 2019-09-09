#include "archive_file_lookup.hpp"

#include <cassert>

#include <vefs/platform/thread_pool.hpp>
#include <vefs/utils/ref_ptr.hpp>

#include "detail/archive_index_file.hpp"

namespace vefs
{
    void archive::file_handle::add_reference()
    {
        mData->add_ext_reference();
    }
    void archive::file_handle::release()
    {
        mData->ext_release();
    }


    archive::file_lookup::file_lookup(detail::basic_archive_file_meta &&meta,
        std::string name, int ibPos, int numBlocks)
        : mRefs{ 1 }
        , mExtRefs{ 0 }
        , mWorkingSet{}
        , mSync{}
        , mIndexBlockPosition{ ibPos }
        , mReservedIndexBlocks{ numBlocks }
        , mMeta{ std::move(meta) }
        , mDirtyMetaData{ }
        , mName{ std::move(name) }
        , mWorkingSetStorage{}
    {
    }

    auto archive::file_lookup::open(detail::basic_archive_file_meta &&meta, std::string name, int ibPos, int numBlocks)
        -> result<utils::ref_ptr<file_lookup>>
    {
        return utils::make_ref_counted<file_lookup>(std::move(meta), std::move(name), ibPos, numBlocks);
    }
    auto archive::file_lookup::create(archive &owner, std::string name)
        -> result<std::tuple<utils::ref_ptr<file_lookup>, file_handle>>
    {
        BOOST_OUTCOME_TRY(fileMeta, owner.mArchive->create_file());

        auto self = utils::make_ref_counted<file_lookup>(std::move(fileMeta), std::move(name), -1, 0);
        BOOST_OUTCOME_TRY(ws, self->create_working_set(owner));

        // basically this would happen if you called load() for an unloaded file
        self->add_ext_reference();
        self->add_reference();
        // the handle here keeps track of the external reference
        // this is important in case create_self() fails
        file_handle h{ *self };

        // create_self can only fail for oom reasons therefore its okay
        // that no external references calls sync which in this case does nothing
        BOOST_OUTCOME_TRY(ws->create_self());

        self->mDirtyMetaData.mark();

        return std::tuple{ std::move(self), std::move(h) };
    }
    archive::file_lookup::~file_lookup()
    {
        assert(mRefs.load(std::memory_order_acquire) == 0);
        assert(!mWorkingSet.load(std::memory_order_acquire));
    }

    auto archive::file_lookup::load(archive &owner)
        -> result<file_handle>
    {
        std::shared_lock rguard{ mSync };
        auto oldState = mExtRefs.fetch_add(1, std::memory_order_acq_rel);
        if (oldState & DeadBit)
        {
            return errc::entry_was_disposed;
        }
        // trap the acquired reference
        file_handle h{ *this };

        if (!mWorkingSet.load(std::memory_order_acquire))
        {
            rguard.unlock();
            std::lock_guard wguard{ mSync };
            if (!mWorkingSet.load(std::memory_order_acquire))
            {
                if (auto cwrx = create_working_set(owner); !cwrx)
                {
                    return cwrx.as_failure();
                }
                // add a self reference which must be released when resetting mWorkingSet
                add_reference();
            }
        }
        return std::move(h);
    }
    auto archive::file_lookup::try_load()
        -> result<file_handle>
    {
        auto oldState = mExtRefs.load(std::memory_order_acquire);
        do
        {
            if (oldState == 0)
            {
                return errc::not_loaded;
            }
            if (oldState & DeadBit)
            {
                return errc::entry_was_disposed;
            }
        } while (mExtRefs.compare_exchange_weak(oldState, oldState + 1,
                    std::memory_order_acq_rel));

        return file_handle{ *this };
    }
    auto archive::file_lookup::try_kill(archive &owner)
        -> result<void>
    {
        // we only allow deletion of files which don't have any open file handles
        std::lock_guard wguard{ mSync };
        std::uint32_t extRefs = 0;
        if (!mExtRefs.compare_exchange_strong(extRefs, DeadBit,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return errc::still_in_use;
        }

        // we need to temporarily load the file in order to free the occupied sectors
        archive::file *ws = mWorkingSet.load(std::memory_order_acquire);
        if (!ws)
        {
            auto rx = create_working_set(owner);
            if (!rx)
            {
                // at this stage, everything is still intact, so its better to
                // stop and report the failure (likely out of memory)
                return rx.as_failure();
            }
            add_reference();
            ws = rx.assume_value();
        }
        // we ignore any failure to free the occupied sectors
        // the next orphan collection will take care of them
        (void)ws->erase_self();

        std::destroy_at(ws);
        mWorkingSet.store(nullptr, std::memory_order_release);
        release();

        return outcome::success();
    }

    auto archive::file_lookup::create_working_set(archive &owner)
        -> result<archive::file *>
    {
        try
        {
            auto ws = new(&mWorkingSetStorage) archive::file(owner, mMeta, *this);
            mWorkingSet.store(ws, std::memory_order_release);
            return ws;
        }
        catch (const std::bad_alloc &)
        {
            // archive::file() can fail to allocate the block pool
            return errc::not_enough_memory;
        }
    }

    auto archive::file_lookup::notify_no_external_references() const
        -> result<void>
    {
        std::lock_guard wguard{ mSync };
        if (mExtRefs.load(std::memory_order_acquire) != 0)
        {
            // either loaded again or killed
            return outcome::success();
        }
        auto ws = mWorkingSet.load(std::memory_order_acquire);
        if (!ws)
        {
            // some other notify_no... call was faster
            return outcome::success();
        }

        BOOST_OUTCOME_TRY(ws->sync());

        std::destroy_at(ws);
        mWorkingSet.store(nullptr, std::memory_order_release);
        release();

        /* #TODO async lookup close */

        return outcome::success();
    }

    void archive::file_lookup::on_sector_write_suggestion(sector_handle sector)
    {
        std::shared_lock rguard{ mSync };
        auto ws = mWorkingSet.load(std::memory_order_acquire);
        if (!ws || !sector.is_dirty())
        {
            return;
        }
        auto maybe_self = make_ref_ptr(this, utils::ref_ptr_acquire);

        ws->owner_ref().ops_pool()
            .execute([maybe_self = std::move(maybe_self), sector]()
        {
            std::shared_lock rguard{ maybe_self->mSync };
            auto ws = maybe_self->mWorkingSet.load(std::memory_order_acquire);
            if (ws && sector.is_dirty())
            {
                // #TODO keep track of async persistence failures
                (void)ws->write_sector_to_disk(std::move(sector));
            }
        });
    }
    void archive::file_lookup::on_root_sector_synced(
        [[maybe_unused]] detail::basic_archive_file_meta &rootMeta)
    {
        mDirtyMetaData.mark();
        if (auto ws = mWorkingSet.load(std::memory_order_acquire))
        {
            ws->mOwner.mArchiveIndexFile->notify_meta_update(
                file_lookup_ptr{ this, utils::ref_ptr_acquire }, ws
            );
        }
    }
    void archive::file_lookup::on_sector_synced([[maybe_unused]] detail::sector_id physId,
        [[maybe_unused]] ro_blob<16> mac)
    {
    }
}
