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
#include <vefs/detail/raw_archive.hpp>
#include <vefs/detail/archive_file.hpp>
#include <vefs/detail/tree_walker.hpp>


namespace vefs::detail
{
    class file_sector_id
    {
        using storage_type = std::uint64_t;
        static constexpr storage_type position_mask = 0x00FF'FFFF'FFFF'FFFF;
        static constexpr storage_type layer_mask = ~position_mask;
        static constexpr auto layer_offset = 56;

    public:
        static constexpr auto references_per_sector = lut::references_per_sector;

        file_sector_id();
        file_sector_id(const vefs::detail::file_id &fileId,
                std::uint64_t position, std::uint8_t layer);

        const file_id & file_id() const noexcept;

        std::uint8_t layer() const noexcept;
        void layer(std::uint8_t value) noexcept;

        std::uint64_t position() const noexcept;
        void position(std::uint64_t value) noexcept;
        std::size_t position_array_offset() const noexcept;

        storage_type layer_position() const noexcept;
        void layer_position(storage_type value);

        bool is_allocated(std::uint64_t fileSize) const;

        explicit operator bool() const noexcept;

        file_sector_id parent() const noexcept;

    private:
        static auto combine(std::uint64_t position, std::uint8_t layer)
            -> storage_type;

        vefs::detail::file_id mFileId;
        // 8b layer pos + 56b sector position on that layer
        storage_type mLayerPosition;
    };


    class file_sector
    {
    public:
        file_sector(file_sector_id logicalId, sector_id physId) noexcept;
        file_sector(detail::raw_archive &src, const detail::raw_archive_file &file,
            const file_sector_id &logicalId, sector_id physId, blob_view mac);

        const auto & sector() const noexcept;

        const auto & id() const noexcept;

        auto data() noexcept;
        auto data() const noexcept;
        auto data_view() const noexcept;

        auto & dirty_flag();
        const auto & dirty_flag() const;

        auto & write_mutex();
        auto & write_queued_flag();

    private:
        file_sector_id mId;
        detail::sector_id mSector;
        std::mutex mWriteMutex{};
        std::atomic<bool> mDirtyFlag{ false };
        std::atomic_flag mWriteQueued = ATOMIC_FLAG_INIT;
        std::array<std::byte, detail::raw_archive::sector_payload_size> mBlockData;
    };

    std::string to_string(const file_sector_id &id);
}

namespace vefs::detail
{
    #pragma region file_sector_id implementation

    inline auto file_sector_id::combine(std::uint64_t position, std::uint8_t layer)
        -> storage_type
    {
        return (static_cast<storage_type>(layer) << layer_offset) | (position & position_mask);
    }

    inline file_sector_id::file_sector_id()
        : mFileId{}
        , mLayerPosition{std::numeric_limits<storage_type>::max()}
    {
    }
    inline file_sector_id::file_sector_id(const vefs::detail::file_id & fileId,
            std::uint64_t position, std::uint8_t layer)
        : mFileId{fileId}
        , mLayerPosition{combine(position, layer)}
    {
    }

    inline const file_id & file_sector_id::file_id() const noexcept
    {
        return mFileId;
    }

    inline std::uint8_t file_sector_id::layer() const noexcept
    {
        return *(reinterpret_cast<const std::uint8_t *>(&mLayerPosition) + 7);
    }

    inline void file_sector_id::layer(std::uint8_t value) noexcept
    {
        *(reinterpret_cast<std::uint8_t *>(&mLayerPosition) + 7) = value;
    }

    inline std::uint64_t file_sector_id::position() const noexcept
    {
        return mLayerPosition & position_mask;
    }

    inline void file_sector_id::position(std::uint64_t value) noexcept
    {
        mLayerPosition = (mLayerPosition & layer_mask) | (value & position_mask);
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
        return mFileId && mLayerPosition != std::numeric_limits<storage_type>::max();
    }

    inline file_sector_id file_sector_id::parent() const noexcept
    {
        auto l = layer();
        l += 1;
        return file_sector_id{ mFileId, position() / references_per_sector, l };
    }

    #pragma endregion

    #pragma region file_sector implementation

    inline auto file_sector::data() noexcept
    {
        return blob{ mBlockData };
    }

    inline file_sector::file_sector(file_sector_id logicalId, sector_id physId) noexcept
        : mId{logicalId}
        , mSector{physId}
        , mBlockData{}
    {
    }

    inline file_sector::file_sector(detail::raw_archive & src, const detail::raw_archive_file & file, const file_sector_id & logicalId, sector_id physId, blob_view mac)
        : file_sector{logicalId, physId}
    {
        src.read_sector(data(), file, mSector, mac);
    }

    inline const auto & file_sector::sector() const noexcept
    {
        return mSector;
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

    inline auto & file_sector::dirty_flag()
    {
        return mDirtyFlag;
    }

    inline const auto & file_sector::dirty_flag() const
    {
        return mDirtyFlag;
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
