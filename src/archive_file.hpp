#pragma once

#include <cstddef>

#include <memory>
#include <shared_mutex>

#include <vefs/archive.hpp>
#include <vefs/detail/cache.hpp>
#include <vefs/detail/tree_walker.hpp>
#include <vefs/detail/basic_archive_file_meta.hpp>
#include <vefs/utils/dirt_flag.hpp>

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
            inline sector(file &owner, handle parent, tree_position position,
                detail::sector_id sectorId, blob_view mac);

            inline detail::sector_id sector_id() const;
            inline tree_position position() const;

            inline auto parent() const -> const handle &;
            inline void update_parent(handle newParent);

            inline blob data();
            inline blob_view data() const;
            inline blob_view data_view() const;

            inline std::shared_mutex & data_sync();
            inline std::atomic_flag & write_queued_flag();

        protected:
            std::shared_mutex mDataSync;
            tree_position mPosition;
            detail::sector_id mSectorId;
            std::atomic_flag mWriteQueued = ATOMIC_FLAG_INIT;
            handle mParent;
            std::array<std::byte, detail::raw_archive::sector_payload_size> mBlockData;
        };

    private:
        using block_pool_t = detail::cache<tree_position, sector, 1 << 6>;

    public:
        using create_tag = archive::create_tag;
        static constexpr create_tag create = archive::create;

        file(archive &owner, detail::basic_archive_file_meta &data, file_events &hooks);
        file(archive &owner, detail::basic_archive_file_meta &data, file_events &hooks, create_tag);
        ~file();

        sector::handle access(tree_position sectorPosition);
        sector::handle access_or_append(tree_position position);

        void read(blob buffer, std::uint64_t readPos);
        void write(blob_view data, std::uint64_t writePos);

        std::uint64_t size();
        void resize(std::uint64_t size);

        void sync();
        //bool is_dirty();

        void erase_self();

        archive & owner_ref();

        void write_sector_to_disk(sector::handle sector);

    private:
        std::optional<file::sector::handle> access_impl(tree_position sectorPosition);

        // the caller is required to hold the shrink lock during the grow_file() call
        void grow_file(std::uint64_t size);
        // the caller is required to uniquely lock the shrink mutex during the call
        void shrink_file(const std::uint64_t size);


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
        virtual void on_sector_synced(detail::sector_id physId, blob_view mac) = 0;
    };
}

namespace vefs
{
    inline archive & archive::file::owner_ref()
    {
        return mOwner;
    }

    inline std::uint64_t archive::file::size()
    {
        std::shared_lock<std::shared_mutex> intLock{ integrity_mutex };
        return mData.size;
    }

    inline archive::file::sector::sector(handle parent, detail::tree_position position,
        detail::sector_id sectorId) noexcept
        : mDataSync{}
        , mPosition{ position }
        , mSectorId{ sectorId }
        , mParent{ std::move(parent) }
        , mBlockData{}
    {
    }
    inline archive::file::sector::sector(file &owner, handle parent,
        detail::tree_position position, detail::sector_id sectorId, blob_view mac)
        : sector{ std::move(parent), position, sectorId }
    {
        owner.mOwner.mArchive->read_sector(data(), owner.mData, sectorId, mac);
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

    inline blob archive::file::sector::data()
    {
        return blob{ mBlockData };
    }
    inline blob_view archive::file::sector::data() const
    {
        return blob_view{ mBlockData };
    }
    inline blob_view archive::file::sector::data_view() const
    {
        return blob_view{ mBlockData };
    }

    inline std::shared_mutex & archive::file::sector::data_sync()
    {
        return mDataSync;
    }
    inline std::atomic_flag & archive::file::sector::write_queued_flag()
    {
        return mWriteQueued;
    }
}
