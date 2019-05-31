#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>

#include <array>
#include <limits>
#include <functional>

#include <fmt/format.h>

#include <boost/iterator/iterator_facade.hpp>

#include <vefs/disappointment/error_detail.hpp>
#include <vefs/utils/hash/default_weak.hpp>
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

        /**
        * adds the position to layer position
        */
        static constexpr auto combine(std::uint64_t position, int layer)
            ->storage_type;

    public:
        /**
         * sets the position inside a layer
         */
        constexpr tree_position() noexcept;
        explicit constexpr tree_position(std::uint64_t pos) noexcept;
        constexpr tree_position(std::uint64_t pos, int layer) noexcept;

        constexpr int layer() const noexcept;

        constexpr void layer(int layer_no) noexcept;

        constexpr std::uint64_t position() const noexcept;

        constexpr void position(std::uint64_t position) noexcept;

        constexpr tree_position parent() const noexcept;
        constexpr int parent_array_offset() const;

        constexpr storage_type raw() const noexcept;

        explicit constexpr operator bool() const noexcept;

    private:
        // 8bit layer pos + 56bit sector position on that layer
        storage_type mLayerPosition;
    };

    template <typename Impl>
    inline void compute_hash(const tree_position &obj, Impl &state);
    template <typename Impl, typename H>
    inline void compute_hash(const tree_position &obj, H &h,
                             utils::hash::algorithm_tag<Impl>);

    /**
     * encapsulates the representation of a path through the tree and the
     * calculation of the path from root through the tree to a target position
     */
    class tree_path
    {
        
        struct waypoint
        {
            std::uint64_t absolute;
            // position within layer
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

        inline tree_path next() const;
        inline tree_path previous() const;

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

    /**
     * adds the layer to position. The schema is layer_bits | position_bits.
     * #Todo rename?
     */
    constexpr auto tree_position::combine(std::uint64_t position, int layer)
        -> storage_type
    /**
    * adds the layer to position
    * @Todo rename?
    */
    {
        return (static_cast<storage_type>(layer) << layer_offset) | (position & position_mask);
    }

    constexpr tree_position::tree_position() noexcept
        : mLayerPosition{ std::numeric_limits<storage_type>::max() }
    {
    }
    /**
     * sets the current position inside the tree
     * @param position inside the layer
     * @param layer layer number
     */
    constexpr tree_position::tree_position(std::uint64_t position, int layer) noexcept
        : mLayerPosition{ combine(position, layer) }
    {
    }
    constexpr tree_position::tree_position(std::uint64_t pos) noexcept
        : tree_position(pos, 0)
    {
    }

    constexpr int tree_position::layer() const noexcept
    {
        return static_cast<int>((mLayerPosition & layer_mask) >> layer_offset);
    }

    constexpr void tree_position::layer(int layer_no) noexcept
    {
        mLayerPosition = (mLayerPosition & position_mask) | static_cast<storage_type>(layer_no)
                                                                << 56;
    }

    constexpr std::uint64_t tree_position::position() const noexcept
    {
        return mLayerPosition & position_mask;
    }

    constexpr void tree_position::position(std::uint64_t position) noexcept
    {
        mLayerPosition = (mLayerPosition & layer_mask) | (position & position_mask);
    }

    constexpr tree_position::operator bool() const noexcept
    {
        return mLayerPosition != std::numeric_limits<storage_type>::max();
    }

    constexpr tree_position vefs::detail::tree_position::parent() const noexcept
    {
        return tree_position{ position() / lut::references_per_sector, layer() + 1 };
    }

     /**
     * @return the offset of the parent node within the sector
     */
    inline constexpr int tree_position::parent_array_offset() const
    {
        return position() % lut::references_per_sector;
    }

    constexpr tree_position::storage_type tree_position::raw() const noexcept
    {
        return mLayerPosition;
    }


    constexpr bool operator==(tree_position lhs, tree_position rhs)
    {
        return lhs.raw() == rhs.raw();
    }
    constexpr bool operator!=(tree_position lhs, tree_position rhs)
    {
        return !(lhs == rhs);
    }


    template <typename Impl>
    inline void compute_hash(const tree_position &obj, Impl &state)
    {
        utils::compute_hash(obj.raw(), state);
    }
    template <typename Impl, typename H>
    inline void compute_hash(const tree_position &obj, H &h,
                             utils::hash::algorithm_tag<Impl>)
    {
        utils::compute_hash(obj.raw(), h, utils::hash::algorithm_tag<Impl>{});
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
            p.absolute = std::numeric_limits<decltype(p.absolute)>::max();
            p.offset = std::numeric_limits<decltype(p.offset)>::max();
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
            // #Todo put this in inline function?
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
        return const_iterator{*this, mTargetLayer - 1};
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

    inline tree_path tree_path::next() const
    {
        return tree_path(mTreeDepth, position(mTargetLayer) + 1, mTargetLayer);
    }

    inline tree_path tree_path::previous() const
    {
        return tree_path(mTreeDepth, position(mTargetLayer) - 1, mTargetLayer);
    }

    #pragma endregion
}

namespace fmt
{
    template <>
    struct formatter<vefs::detail::tree_position>
    {
        template <typename ParseContext>
        constexpr auto parse(ParseContext &ctx)
        {
            return ctx.begin();
        }

        template <typename FormatContext>
        auto format(const vefs::detail::tree_position &tp, FormatContext &ctx)
        {
            return format_to(ctx.begin(), "(L{}, P{:#04x})", tp.layer(), tp.position());
        }
    };
}

namespace vefs::ed
{
    enum class sector_tree_position_tag {};
    using sector_tree_position = error_detail<sector_tree_position_tag, detail::tree_position>;
}
