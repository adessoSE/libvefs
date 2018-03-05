#include "precompiled.hpp"
#include "archive_file_lookup.hpp"

#include <cassert>

#include <vefs/utils/ref_ptr.hpp>

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


    archive::file_lookup::file_lookup(detail::basic_archive_file_meta &meta)
        : mRefs{ 1 }
        , mExtRefs{ 0 }
        , mWorkingSet{}
        , mSync{}
        , mMeta{ std::move(meta) }
        , mWorkingSetStorage{}
    {
    }
    archive::file_lookup::file_lookup(detail::basic_archive_file_meta &meta,
        archive &owner, file::create_tag)
        : file_lookup{ meta }
    {
        // #ReplaceBandaid
        auto ws = create_working_set(owner);
        add_reference();
        add_ext_reference();
        try
        {
            ws->access_or_append(detail::tree_position{ 0, 0 });
        }
        catch (...)
        {
            ext_release();
            throw;
        }
    }
    archive::file_lookup::~file_lookup()
    {
        assert(mRefs.load(std::memory_order_acquire) == 0);
        assert(!mWorkingSet);
    }

    archive::file_handle archive::file_lookup::load(archive &owner)
    {
        {
            std::shared_lock<std::shared_mutex> rguard{ mSync };
            auto oldState = mExtRefs.fetch_add(1, std::memory_order_acq_rel);
            if (oldState & DeadBit)
            {
                return {};
            }

            if (!mWorkingSet.load(std::memory_order_acquire))
            {
                rguard.unlock();
                std::lock_guard<std::shared_mutex> wguard{ mSync };
                if (!mWorkingSet.load(std::memory_order_acquire))
                {
                    create_working_set(owner);
                    // add a self reference which must be released when resetting mWorkingSet
                    add_reference();
                }
            }
        }
        return file_handle{ *this };
    }
    bool archive::file_lookup::try_kill(archive &owner)
    {
        // we only allow deletion of files which don't have any open file handles
        std::lock_guard<std::shared_mutex> wguard{ mSync };
        std::uint32_t extRefs = 0;
        if (!mExtRefs.compare_exchange_strong(extRefs, DeadBit,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return false;
        }

        // we need to temporarily load the file in order to free the occupied sectors
        archive::file *ws = mWorkingSet.load(std::memory_order_acquire);
        if (!ws)
        {
            ws = create_working_set(owner);
            add_reference();
        }

        ws->erase_self();

        std::destroy_at(ws);
        mWorkingSet.store(nullptr, std::memory_order_release);
        release();

        return true;
    }

    archive::file * archive::file_lookup::create_working_set(archive &owner)
    {
        // #TODO #ReplaceBandaid there must be a better way to manage the ownership for dirty writes
        auto ws = new(&mWorkingSetStorage) archive::file(owner, mMeta, [this](auto sector)
        {
            std::shared_lock<std::shared_mutex> rguard{ mSync };
            auto ws = mWorkingSet.load(std::memory_order_acquire);
            if (!ws || !sector.is_dirty())
            {
                return;
            }
            auto maybe_self = make_ref_ptr(this, utils::ref_ptr_acquire);

            ws->owner_ref().mOpsPool
                ->exec([maybe_self = std::move(maybe_self), sector]()
            {
                std::shared_lock<std::shared_mutex> rguard{ maybe_self->mSync };
                auto ws = maybe_self->mWorkingSet
                    .load(std::memory_order_acquire);
                if (ws && sector.is_dirty())
                {
                    ws->write_sector_to_disk(std::move(sector));
                }
            });
        });
        mWorkingSet.store(ws, std::memory_order_release);
        return ws;
    }

    void archive::file_lookup::notify_no_external_references() const
    {
        std::lock_guard<std::shared_mutex> rguard{ mSync };
        if (mExtRefs.load(std::memory_order_acquire) != 0)
        {
            // either loaded again or killed
            return;
        }
        auto ws = mWorkingSet.load(std::memory_order_acquire);
        if (!ws)
        {
            // someone other notify_no... call was faster
            return;
        }

        ws->sync();

        std::destroy_at(ws);
        mWorkingSet.store(nullptr, std::memory_order_release);
        release();

        /* #TODO async lookup close

        auto self = make_ref_ptr(const_cast<file_lookup *>(this), utils::ref_ptr_acquire);
        owner.mOpsPool->exec([self = std::move(self)]()
        {
            std::unique_lock<std::shared_mutex> rguard{ self->mSync };
            if (self->mExtRefs.load(std::memory_order_acquire) != 0)
            {
                return;
            }
            auto ws = self->mWorkingSet.load(std::memory_order_acquire);
            if (!ws)
            {
                return;
            }

            if (ws->is_dirty())
            {
                self->add_ext_reference();
                rguard.unlock();

                try
                {
                    ws->sync();
                }
                catch (...)
                {
                    std::cerr << "unexpected error in async cleanup task" << std::endl;
                }
                self->ext_release();

            }
        });
        //*/
    }
}
