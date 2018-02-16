#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <mutex>
#include <atomic>

#include <boost/uuid/uuid.hpp>
#include <boost/functional/hash_fwd.hpp>

#include <vefs/blob.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/detail/sector_id.hpp>
#include <vefs/detail/cache.hpp>
#include <vefs/detail/raw_archive.hpp>
#include <vefs/detail/archive_file.hpp>
#include <vefs/detail/tree_walker.hpp>


namespace vefs::detail
{
    class file_sector_id
    {
    public:
        using storage_type = tree_position;

        static constexpr auto references_per_sector = lut::references_per_sector;

        file_sector_id();
        file_sector_id(const vefs::detail::file_id &fileId,
                tree_position position);

        const file_id & file_id() const noexcept;

        int layer() const noexcept;
        void layer(int value) noexcept;

        std::uint64_t position() const noexcept;
        void position(std::uint64_t value) noexcept;
        std::size_t position_array_offset() const noexcept;

        storage_type layer_position() const noexcept;
        void layer_position(storage_type value);

        bool is_allocated(std::uint64_t fileSize) const;

        explicit operator bool() const noexcept;

        file_sector_id parent() const noexcept;

    private:
        vefs::detail::file_id mFileId;
        storage_type mLayerPosition;
    };


    class file_sector
    {
    public:
        using handle = cache_handle<file_sector>;

        file_sector(handle parentSector, file_sector_id logicalId, sector_id physId) noexcept;
        file_sector(raw_archive &src, const raw_archive_file &file,
            handle parentSector, const file_sector_id &logicalId,
            sector_id physId, blob_view mac);

        const auto & sector() const noexcept;
        const auto & parent() const noexcept;
        void update_parent(handle newParent);

        const auto & id() const noexcept;

        auto data() noexcept;
        auto data() const noexcept;
        auto data_view() const noexcept;

        auto & write_mutex();
        auto & write_queued_flag();

    private:
        file_sector_id mId;
        sector_id mSector;
        handle mParentSector;
        std::mutex mWriteMutex{};
        std::atomic_flag mWriteQueued = ATOMIC_FLAG_INIT;
        std::array<std::byte, raw_archive::sector_payload_size> mBlockData;
    };

    std::string to_string(const file_sector_id &id);
}

namespace vefs::detail
{
    #pragma region file_sector_id implementation

    inline file_sector_id::file_sector_id()
        : mFileId{}
        , mLayerPosition{std::numeric_limits<storage_type>::max()}
    {
    }
    inline file_sector_id::file_sector_id(const vefs::detail::file_id & fileId,
            tree_position position)
        : mFileId{fileId}
        , mLayerPosition{position}
    {
    }

    inline const file_id & file_sector_id::file_id() const noexcept
    {
        return mFileId;
    }

    inline int file_sector_id::layer() const noexcept
    {
        return mLayerPosition.layer();
    }

    inline void file_sector_id::layer(int value) noexcept
    {
        mLayerPosition.layer(value);
    }

    inline std::uint64_t file_sector_id::position() const noexcept
    {
        return mLayerPosition.position();
    }

    inline void file_sector_id::position(std::uint64_t value) noexcept
    {
        mLayerPosition.position(value);
    }

    inline std::size_t file_sector_id::position_array_offset() const noexcept
    {
        return position() % references_per_sector;
    }

    inline file_sector_id::storage_type file_sector_id::layer_position() const noexcept
    {
        return mLayerPosition;
    }

    inline void file_sector_id::layer_position(storage_type value)
    {
        mLayerPosition = value;
    }

    inline bool file_sector_id::is_allocated(const std::uint64_t fileSize) const
    {
        const auto l = layer();
        const auto pos = position();
        const auto unit_width = lut::step_width[l]; // width of the referenced layer
        const auto step_width = lut::step_width[l + 1]; // step width on the reference layer
        const auto beginPos = pos * step_width;

        return ((pos | l) == 0) // there is always a sector allocated for each file
            || unit_width < fileSize && beginPos < fileSize;
    }

    inline file_sector_id::operator bool() const noexcept
    {
        return mFileId && mLayerPosition;
    }

    inline file_sector_id file_sector_id::parent() const noexcept
    {
        return file_sector_id{ mFileId, mLayerPosition.parent() };
    }

    #pragma endregion

    #pragma region file_sector implementation

    inline auto file_sector::data() noexcept
    {
        return blob{ mBlockData };
    }

    inline file_sector::file_sector(handle parentSector, file_sector_id logicalId, sector_id physId) noexcept
        : mId{logicalId}
        , mSector{physId}
        , mParentSector{std::move(parentSector)}
        , mBlockData{}
    {
    }

    inline file_sector::file_sector(raw_archive & src, const raw_archive_file & file,
        handle parentSector, const file_sector_id & logicalId, sector_id physId, blob_view mac)
        : file_sector{std::move(parentSector), logicalId, physId}
    {
        src.read_sector(data(), file, mSector, mac);
    }

    inline const auto & file_sector::sector() const noexcept
    {
        return mSector;
    }

    inline const auto & file_sector::parent() const noexcept
    {
        return mParentSector;
    }

    inline void file_sector::update_parent(handle newParent)
    {
        mParentSector = std::move(newParent);
    }

    inline const auto & file_sector::id() const noexcept
    {
        return mId;
    }

    inline auto file_sector::data() const noexcept
    {
        return blob_view{ mBlockData };
    }

    inline auto file_sector::data_view() const noexcept
    {
        return blob_view{ mBlockData };
    }

    inline auto & file_sector::write_mutex()
    {
        return mWriteMutex;
    }

    inline auto & file_sector::write_queued_flag()
    {
        return mWriteQueued;
    }

    inline bool operator==(const file_sector_id &lhs, const file_sector_id &rhs)
    {
        return lhs.file_id() == rhs.file_id() && lhs.layer_position() == rhs.layer_position();
    }
    inline bool operator!=(const file_sector_id &lhs, const file_sector_id &rhs)
    {
        return !(lhs == rhs);
    }

    #pragma endregion
}

namespace std
{
    template <>
    struct hash<vefs::detail::file_sector_id>
    {
        std::size_t operator()(const vefs::detail::file_sector_id &id)
        {
            //TODO: do something better than this!
            static_assert(sizeof(vefs::detail::file_sector_id) == 24);
            auto begin = reinterpret_cast<const std::uint64_t *>(&id);
            auto end = begin + sizeof(vefs::detail::file_sector_id) / sizeof(std::uint64_t);
            return boost::hash_range(begin, end);
        }
    };
}
