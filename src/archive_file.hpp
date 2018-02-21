#pragma once

#include <cstddef>

#include <memory>
#include <shared_mutex>

#include <vefs/detail/archive_file.hpp>
#include <vefs/detail/string_map.hpp>
#include <vefs/archive.hpp>

namespace vefs
{
    class archive::file : public std::enable_shared_from_this<archive::file>
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

        private:
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

        file(archive &owner, detail::raw_archive_file &data);
        file(archive &owner, detail::raw_archive_file &data, create_tag);
        ~file();

        sector::handle access(tree_position sectorPosition);
        sector::handle access_or_append(tree_position position);

        void read(blob buffer, std::uint64_t readPos);
        void write(blob_view data, std::uint64_t writePos);

        std::uint64_t size();
        void resize(std::uint64_t size);

        void sync();

        archive & owner_ref();

    private:
        std::optional<file::sector::handle> access_impl(tree_position sectorPosition);

        // the caller is required to hold the shrink lock during the grow_file() call
        void grow_file(std::uint64_t size);
        // the caller is required to uniquely lock the shrink mutex during the call
        void shrink_file(const std::uint64_t size);

        void write_sector_to_disk(sector::handle sector);

        archive &mOwner;
        detail::raw_archive_file &mData;
        std::unique_ptr<block_pool_t> mCachedBlocks;
    };
}

namespace vefs
{
    inline archive::file_handle archive::file_lookup::to_handle(archive &owner)
    {
        if (!handle)
        {
            handle = std::make_shared<archive::file>(owner, *persistent);
        }
        return handle;
    }

    inline archive & archive::file::owner_ref()
    {
        return mOwner;
    }

    inline std::uint64_t archive::file::size()
    {
        std::shared_lock<std::shared_mutex> intLock{ mData.integrity_mutex };
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
