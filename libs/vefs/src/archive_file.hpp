#pragma once

#include <cstddef>

#include <memory>
#include <shared_mutex>

#include <vefs/archive.hpp>
#include <vefs/utils/dirt_flag.hpp>

#include "detail/basic_archive_file_meta.hpp"
#include "detail/cache_car.hpp"
#include "detail/tree_walker.hpp"

namespace vefs
{
    class file_events;

    class archive::file
    {
        friend class archive;

    public:
        using tree_position = detail::tree_position;

        class sector
        {
        public:
            using handle = detail::cache_handle<sector>;

            inline sector(handle parent, tree_position position,
                          detail::sector_id sectorId) noexcept;

            inline detail::sector_id sector_id() const;
            inline tree_position position() const;

            inline auto parent() const -> const handle &;
            inline void update_parent(handle newParent);

            inline auto data() -> rw_blob<detail::raw_archive::sector_payload_size>;
            inline auto data() const -> ro_blob<detail::raw_archive::sector_payload_size>;
            inline auto data_view() const -> ro_blob<detail::raw_archive::sector_payload_size>;

            inline std::shared_mutex &data_sync();
            inline std::atomic_flag &write_queued_flag();

        protected:
            std::shared_mutex mDataSync;
            tree_position mPosition;
            detail::sector_id mSectorId;
            std::atomic_flag mWriteQueued = ATOMIC_FLAG_INIT;
            handle mParent;
            std::array<std::byte, detail::raw_archive::sector_payload_size> mBlockData;
        };

    private:
        using block_pool_t = detail::cache_car<tree_position, sector, 1 << 6>;

    public:
        file(archive &owner, detail::basic_archive_file_meta &data, file_events &hooks);
        ~file();

        auto access(tree_position sectorPosition) -> result<sector::handle>;
        auto access_or_append(tree_position position) -> result<sector::handle>;
        auto try_access(tree_position key) -> sector::handle;

        result<void> read(rw_dynblob buffer, std::uint64_t readPos);
        result<void> write(ro_dynblob data, std::uint64_t writePos);
        std::uint64_t write(sector::handle &sector, ro_dynblob data, std::uint64_t offset);

        std::uint64_t size();
        result<void> resize(std::uint64_t size);

        result<void> sync();
        // bool is_dirty();

        result<void> create_self();
        result<void> erase_self();

        archive &owner_ref();

        result<void> write_sector_to_disk(sector::handle sector);

        auto lock_integrity() -> std::unique_lock<std::shared_mutex>;

    protected:
        std::uint64_t write_no_lock(sector::handle &sector, ro_dynblob data, std::uint64_t offset);

    private:
        auto access_impl(tree_position sectorPosition) -> result<sector::handle>;

        // the caller is required to hold the shrink lock during the grow_file() call
        result<void> grow_file(std::uint64_t size);
        // the caller is required to uniquely lock the shrink mutex during the call
        result<void> shrink_file(const std::uint64_t size);

        archive &mOwner;
        file_events &mHooks;
        detail::basic_archive_file_meta &mData;

        // always lock the shrink_mutex first!
        mutable std::shared_mutex integrity_mutex;
        mutable std::shared_mutex shrink_mutex;

        std::unique_ptr<block_pool_t> mCachedBlocks;

        utils::dirt_flag mWriteFlag;
    };

    class file_events
    {
    public:
        using sector_handle = detail::cache_handle<archive::file::sector>;

        virtual void on_sector_write_suggestion(sector_handle sector) = 0;
        virtual void on_root_sector_synced(detail::basic_archive_file_meta &rootMeta) = 0;
        virtual void on_sector_synced(detail::sector_id physId, ro_blob<16> mac) = 0;
    };
} // namespace vefs

namespace vefs
{
    inline archive &archive::file::owner_ref()
    {
        return mOwner;
    }

    inline auto archive::file::try_access(tree_position key) -> file::sector::handle
    {
        return mCachedBlocks->try_access(key);
    }

    inline std::uint64_t archive::file::size()
    {
        std::shared_lock<std::shared_mutex> intLock{integrity_mutex};
        return mData.size;
    }

    inline archive::file::sector::sector(handle parent, detail::tree_position position,
                                         detail::sector_id sectorId) noexcept
        : mDataSync{}
        , mPosition{position}
        , mSectorId{sectorId}
        , mParent{std::move(parent)}
        , mBlockData{}
    {
    }

    inline detail::sector_id archive::file::sector::sector_id() const
    {
        return mSectorId;
    }
    inline detail::tree_position archive::file::sector::position() const
    {
        return mPosition;
    }

    inline auto archive::file::sector::parent() const -> const handle &
    {
        return mParent;
    }

    inline void archive::file::sector::update_parent(handle newParent)
    {
        mParent = std::move(newParent);
    }

    inline auto archive::file::sector::data() -> rw_blob<detail::raw_archive::sector_payload_size>
    {
        return mBlockData;
    }
    inline auto archive::file::sector::data() const
        -> ro_blob<detail::raw_archive::sector_payload_size>
    {
        return mBlockData;
    }
    inline auto archive::file::sector::data_view() const
        -> ro_blob<detail::raw_archive::sector_payload_size>
    {
        return mBlockData;
    }

    inline std::shared_mutex &archive::file::sector::data_sync()
    {
        return mDataSync;
    }
    inline std::atomic_flag &archive::file::sector::write_queued_flag()
    {
        return mWriteQueued;
    }
} // namespace vefs
