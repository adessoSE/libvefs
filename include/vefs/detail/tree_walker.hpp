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
    struct tree_position final
    {
    private:
        using storage_type = std::uint64_t;
        static constexpr auto layer_offset = 56;
        static constexpr storage_type layer_mask = static_cast<storage_type>(0xFF) << layer_offset;
        static constexpr storage_type position_mask = ~layer_mask;

        static constexpr auto combine(std::uint64_t position, int layer)
            ->storage_type;

    public:
        constexpr tree_position() noexcept;
        explicit constexpr tree_position(std::uint64_t pos) noexcept;
        constexpr tree_position(std::uint64_t pos, int layer) noexcept;

        inline int layer() const noexcept;
        inline void layer(int value) noexcept;

        inline std::uint64_t position() const noexcept;
        inline void position(std::uint64_t value) noexcept;

        inline tree_position parent() const noexcept;

        inline storage_type raw() const noexcept;
        //void raw(storage_type value) noexcept;

        explicit operator bool() const noexcept;

    private:
        // 8b layer pos + 56b sector position on that layer
        storage_type mLayerPosition;
    };

    class tree_path
    {
        struct waypoint
        {
            std::uint64_t absolute;
            int offset;
        };
        using waypoint_array = std::array<waypoint, lut::max_tree_depth + 2>;

    public:
        class iterator;
        using const_iterator = iterator;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        inline tree_path() noexcept;
        inline tree_path(int treeDepth, tree_position position) noexcept;
        inline tree_path(int treeDepth, std::uint64_t pos, int layer = 0) noexcept;

        inline tree_position layer_position(int layer) const noexcept;
        inline std::uint64_t position(int layer) const noexcept;
        inline int offset(int layer) const noexcept;

        inline explicit operator bool() const noexcept;

        auto begin() const -> const_iterator;
        auto cbegin() const -> const_iterator;
        auto end() const -> const_iterator;
        auto cend() const -> const_iterator;

        auto rbegin() const -> const_reverse_iterator;
        auto crbegin() const -> const_reverse_iterator;
        auto rend() const -> const_reverse_iterator;
        auto crend() const -> const_reverse_iterator;


    private:
        inline tree_path(int treeDepth, int targetLayer) noexcept;

        template <int layer>
        void init(std::uint64_t pos) noexcept;

        waypoint_array mTreePath;
        int mTreeDepth;
        int mTargetLayer;
    };

    class tree_path::iterator
        : public boost::iterator_facade<tree_path::iterator,
            tree_position,
            boost::bidirectional_traversal_tag,
            tree_position
        >
    {
        friend class boost::iterator_core_access;

    public:
        iterator();
        iterator(const tree_path &path);
        iterator(const tree_path &path, int layer);

        int array_offset();

    private:
        bool equal(const iterator &other) const;

        tree_position dereference() const;
        void increment();
        void decrement();

        const tree_path *mOwner;
        int mLayer;
    };
}

namespace vefs::detail
{
    #pragma region tree_position implementation

    constexpr auto tree_position::combine(std::uint64_t position, int layer)
        -> storage_type
    {
        return (static_cast<storage_type>(layer) << layer_offset) | (position & position_mask);
    }

    constexpr tree_position::tree_position() noexcept
        : mLayerPosition{ std::numeric_limits<storage_type>::max() }
    {
    }
    constexpr tree_position::tree_position(std::uint64_t position, int layer) noexcept
        : mLayerPosition{ combine(position, layer) }
    {
    }
    constexpr tree_position::tree_position(std::uint64_t pos) noexcept
        : tree_position(pos, 0)
    {
    }

    inline int tree_position::layer() const noexcept
    {
        return static_cast<int>((mLayerPosition & layer_mask) >> layer_offset);
    }

    inline void tree_position::layer(int value) noexcept
    {
        //*(reinterpret_cast<std::uint8_t *>(&mLayerPosition) + 7)
        //    = static_cast<std::uint8_t>(value);
        mLayerPosition = (mLayerPosition & position_mask)
            | static_cast<storage_type>(value) << 56;
    }

    inline std::uint64_t tree_position::position() const noexcept
    {
        return mLayerPosition & position_mask;
    }

    inline void tree_position::position(std::uint64_t value) noexcept
    {
        mLayerPosition = (mLayerPosition & layer_mask) | (value & position_mask);
    }

    inline tree_position::operator bool() const noexcept
    {
        return mLayerPosition != std::numeric_limits<storage_type>::max();
    }

    inline tree_position vefs::detail::tree_position::parent() const noexcept
    {
        return tree_position{ position() / lut::references_per_sector, layer() + 1 };
    }

    inline tree_position::storage_type tree_position::raw() const noexcept
    {
        return mLayerPosition;
    }

    inline bool operator==(tree_position lhs, tree_position rhs)
    {
        return lhs.raw() == rhs.raw();
    }
    inline bool operator!=(tree_position lhs, tree_position rhs)
    {
        return !(lhs == rhs);
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
        static_assert(layer < 5);
        static_assert(layer >= 0);

        // this lets the compiler use compile time divisor lookups
        // which in turn allows for turning the division into a montgomery multiplication.
        // my benchmarks suggest that this is at least twice as fast as
        // a simple loop.
        switch (mTreeDepth)
        {
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

        case 0:
            mTreePath[mTreeDepth].absolute = 0;
            mTreePath[mTreeDepth].offset = 0;
        }
    }

    inline tree_path::tree_path(int treeDepth, std::uint64_t pos, int layer) noexcept
        : tree_path(treeDepth, layer)
    {
        assert(treeDepth >= 0);
        assert(treeDepth <= 5);
        assert(layer >= 0);
        assert(layer <= treeDepth);

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

        case 5:
            mTreePath[5].absolute = 0;
            mTreePath[5].offset = 0;
            break;
        }
    }

    inline tree_path::tree_path(int treeDepth, tree_position position) noexcept
        : tree_path(treeDepth, position.position(), position.layer())
    {
    }

    inline tree_position tree_path::layer_position(int layer) const noexcept
    {
        return { position(layer), layer };
    }
    inline std::uint64_t tree_path::position(int layer) const noexcept
    {
        return mTreePath[layer].absolute;
    }
    inline int tree_path::offset(int layer) const noexcept
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
    inline tree_path::iterator::iterator(const tree_path &path, int layer)
        : mOwner(&path)
        , mLayer(layer)
    {

    }
    inline tree_path::iterator::iterator(const tree_path &path)
        : iterator(path, path.mTreeDepth)
    {
    }

    inline int tree_path::iterator::array_offset()
    {
        return mOwner->offset(mLayer);
    }

    inline tree_position tree_path::iterator::dereference() const
    {
        return mOwner->layer_position(mLayer);
    }

    inline void tree_path::iterator::increment()
    {
        mLayer -= 1;
    }

    inline void tree_path::iterator::decrement()
    {
        mLayer += 1;
    }

    inline bool tree_path::iterator::equal(const iterator & other) const
    {
        if (mLayer < 0)
        {
            return other.mLayer < 0;
        }
        return mLayer == other.mLayer && mOwner == other.mOwner;
    }


    inline auto tree_path::begin() const
        -> const_iterator
    {
        return const_iterator{ *this };
    }

    inline auto tree_path::cbegin() const
        -> const_iterator
    {
        return begin();
    }

    inline auto tree_path::end() const
        -> const_iterator
    {
        return const_iterator{*this, -1};
    }

    inline auto tree_path::cend() const
        -> const_iterator
    {
        return end();
    }

    inline auto tree_path::rbegin() const
        -> const_reverse_iterator
    {
        return const_reverse_iterator{ end() };
    }

    inline auto tree_path::crbegin() const
        -> const_reverse_iterator
    {
        return rbegin();
    }

    inline auto tree_path::rend() const
        -> const_reverse_iterator
    {
        return const_reverse_iterator{ begin() };
    }

    inline auto tree_path::crend() const
        -> const_reverse_iterator
    {
        return rend();
    }

    #pragma endregion
}