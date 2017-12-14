#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>

#include <array>
#include <limits>

#include <boost/iterator/iterator_facade.hpp>

#include <vefs/detail/raw_archive.hpp>
#include <vefs/detail/tree_lut.hpp>


namespace vefs::detail
{
    struct compact_sector_position
    {
    private:
        using storage_type = std::uint64_t;
        static constexpr storage_type position_mask = 0x00FF'FFFF'FFFF'FFFF;
        static constexpr storage_type layer_mask = ~position_mask;
        static constexpr auto layer_offset = 56;

        static auto combine(std::uint64_t position, std::uint8_t layer)
            ->storage_type;

    public:
        compact_sector_position() noexcept;
        compact_sector_position(std::uint64_t, int layer) noexcept;

        int layer() const noexcept;
        void layer(int value) noexcept;

        std::uint64_t position() const noexcept;
        void position(std::uint64_t value) noexcept;

        //storage_type raw() const noexcept;
        //void raw(storage_type value) noexcept;

    private:
        storage_type mLayerPosition;
    };

    class tree_path
    {
        struct waypoint
        {
            std::uint64_t absolute;
            std::uint64_t offset;
        };
        using waypoint_array = std::array<waypoint, lut::max_tree_depth + 1>;

    public:
        class iterator;

        tree_path() noexcept;
        tree_path(int treeDepth, std::uint64_t pos, int layer = 0) noexcept;

        compact_sector_position layer_position(int layer) const noexcept;
        std::uint64_t position(int layer) const noexcept;
        std::uint64_t offset(int layer) const noexcept;

        explicit operator bool() const noexcept;

    private:
        tree_path(int treeDepth, int targetLayer) noexcept;

        template <int layer>
        void init(std::uint64_t pos) noexcept;

        waypoint_array mTreePath;
        int mTreeDepth;
        int mTargetLayer;
    };

    class tree_path::iterator
        : boost::iterator_facade<tree_path::iterator,
            compact_sector_position,
            boost::forward_traversal_tag,
            compact_sector_position
        >
    {
        friend class boost::iterator_core_access;

    public:
        iterator();
        iterator(const tree_path &path);

    private:
        bool equal(const iterator &other) const;

        compact_sector_position dereference() const;
        void increment();

        const tree_path *mOwner;
        int mLayer;
    };
}

namespace vefs::detail
{
    #pragma region compact_sector_position implementation

    inline auto compact_sector_position::combine(std::uint64_t position, std::uint8_t layer)
        -> storage_type
    {
        return (static_cast<storage_type>(layer) << layer_offset) | (position & position_mask);
    }

    inline compact_sector_position::compact_sector_position() noexcept
        : mLayerPosition{ std::numeric_limits<storage_type>::max() }
    {
    }
    inline compact_sector_position::compact_sector_position(
        std::uint64_t position, int layer) noexcept
        : mLayerPosition{ combine(position, static_cast<std::uint8_t>(layer)) }
    {
    }

    inline int compact_sector_position::layer() const noexcept
    {
        return *(reinterpret_cast<const std::uint8_t *>(&mLayerPosition) + 7);
    }

    inline void compact_sector_position::layer(int value) noexcept
    {
        *(reinterpret_cast<std::uint8_t *>(&mLayerPosition) + 7)
            = static_cast<std::uint8_t>(value);
    }

    inline std::uint64_t compact_sector_position::position() const noexcept
    {
        return mLayerPosition & position_mask;
    }

    inline void compact_sector_position::position(std::uint64_t value) noexcept
    {
        mLayerPosition = (mLayerPosition & layer_mask) | (value & position_mask);
    }

    #pragma endregion

    #pragma region tree_path implementation

    inline tree_path::tree_path(int treeDepth, int targetLayer) noexcept
        : mTreeDepth(treeDepth)
        , mTargetLayer(targetLayer)
    {
#if !defined NDEBUG
        for (auto &p : mTreePath)
        {
            p.absolute = p.offset = std::numeric_limits<std::uint64_t>::max();
        }
#endif
    }

    inline tree_path::tree_path() noexcept
        : tree_path(-1, -1)
    {
    }

    template <int layer>
    inline void tree_path::init(std::uint64_t pos) noexcept
    {
        // this lets the compiler use compile time divisor lookups
        // which in turn allows for turning the division into a montgomery multiplication.
        // my benchmarks suggest that this is at least twice as fast as
        // a simple loop.
        switch (mTreeDepth)
        {
        case 0:
            mTreePath[0].absolute = 0;
            mTreePath[0].offset = 0;
            break;

        case 5:
            mTreePath[4].absolute = pos / lut::ref_width[4 - layer];
            mTreePath[4].offset = mTreePath[4].absolute % lut::references_per_sector;

        case 4:
            if constexpr (layer < 4)
            {
                mTreePath[3].absolute = pos / lut::ref_width[3 - layer];
                mTreePath[3].offset = mTreePath[3].absolute % lut::references_per_sector;
            }

        case 3:
            if constexpr (layer < 3)
            {
                mTreePath[2].absolute = pos / lut::ref_width[2 - layer];
                mTreePath[2].offset = mTreePath[2].absolute % lut::references_per_sector;
            }

        case 2:
            if constexpr (layer < 2)
            {
                mTreePath[1].absolute = pos / lut::ref_width[1 - layer];
                mTreePath[1].offset = mTreePath[1].absolute % lut::references_per_sector;
            }

        case 1:
            if constexpr (layer < 1)
            {
                mTreePath[0].absolute = pos / lut::ref_width[0];
                mTreePath[0].offset = mTreePath[0].absolute % lut::references_per_sector;
            }
        }
    }

    inline tree_path::tree_path(int treeDepth, std::uint64_t pos, int layer) noexcept
        : tree_path(treeDepth, layer)
    {
        assert(treeDepth >= 0);
        assert(treeDepth <= 5);
        assert(layer >= 0);
        assert(layer == 0 || layer < treeDepth);

        switch (layer)
        {
        case 0:
            init<0>(pos);
            break;

        case 1:
            init<1>(pos);
            break;

        case 2:
            init<2>(pos);
            break;

        case 3:
            init<3>(pos);
            break;

        case 4:
            init<4>(pos);
            break;
        }
    }

    inline compact_sector_position tree_path::layer_position(int layer) const noexcept
    {
        return { position(layer), layer };
    }
    inline std::uint64_t tree_path::position(int layer) const noexcept
    {
        return mTreePath[layer].absolute;
    }
    inline std::uint64_t tree_path::offset(int layer) const noexcept
    {
        return mTreePath[layer].offset;
    }

    inline tree_path::operator bool() const noexcept
    {
        return mTreeDepth < 0;
    }

    #pragma endregion

    #pragma region tree_path::iterator implementation

    inline tree_path::iterator::iterator()
        : mOwner(nullptr)
        , mLayer(-1)
    {
    }
    inline tree_path::iterator::iterator(const tree_path &path)
        : mOwner(&path)
        , mLayer(path.mTreeDepth - 1)
    {
        if (mLayer < 0)
        {
            mLayer = 0;
        }
    }

    inline compact_sector_position tree_path::iterator::dereference() const
    {
        return mOwner->layer_position(mLayer);
    }

    inline void tree_path::iterator::increment()
    {
        mLayer -= 1;
    }

    inline bool tree_path::iterator::equal(const iterator & other) const
    {
        if (mLayer < 0)
        {
            return other.mLayer < 0;
        }
        return mLayer == other.mLayer && mOwner == other.mOwner;
    }

    #pragma endregion
}
