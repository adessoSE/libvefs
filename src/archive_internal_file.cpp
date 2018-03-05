#include "precompiled.hpp"
#include "archive_internal_file.hpp"

namespace vefs
{
    archive::internal_file::internal_file(archive &owner, detail::basic_archive_file_meta &meta)
        : std::enable_shared_from_this<archive::internal_file>{}
        , archive::file{ owner, meta, [this](sector::handle sector)
                                      { on_dirty_sector(std::move(sector)); } }
        , mLifetimeSync{}
        , mDisposed{ false }
    {
    }
    archive::internal_file::internal_file(archive & owner, detail::basic_archive_file_meta & meta,
        create_tag)
        : std::enable_shared_from_this<archive::internal_file>{}
        , archive::file{ owner, meta, [this](sector::handle sector)
                                      { on_dirty_sector(std::move(sector)); }, create_tag{} }
        , mLifetimeSync{}
        , mDisposed{ false }
    {
    }

    void archive::internal_file::dispose()
    {
        std::lock_guard<std::shared_mutex> lock{ mLifetimeSync };
        mDisposed = true;
    }

    void archive::internal_file::on_dirty_sector(block_pool_t::handle sector)
    {
        mOwner.mOpsPool->exec([maybe_self = weak_from_this(), sector = std::move(sector)]()
        {
            if (auto self = maybe_self.lock())
            {
                std::shared_lock<std::shared_mutex> lifetimeLock{ self->mLifetimeSync };
                if (!self->mDisposed)
                {
                    self->write_sector_to_disk(std::move(sector));
                }
            }
        });
    }
}
