#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <array>
#include <compare>
#include <functional>
#include <limits>

#include <fmt/format.h>

#include <vefs/disappointment/error_detail.hpp>

#include "sector_device.hpp"
#include "tree_lut.hpp"

namespace vefs::detail
{

struct tree_position final
{
private:
    using storage_type = std::uint64_t;
    static constexpr auto layer_offset = 56;
    static constexpr storage_type layer_mask = static_cast<storage_type>(0xFF)
                                               << layer_offset;
    static constexpr storage_type position_mask = ~layer_mask;

    /**
     * adds the position to layer position
     */
    static constexpr auto compress(std::uint64_t position, int layer)
            -> storage_type;

public:
    /**
     * sets the position inside a layer
     */
    constexpr tree_position() noexcept;
    explicit constexpr tree_position(std::uint64_t pos) noexcept;
    constexpr tree_position(std::uint64_t pos, int layer) noexcept;

    [[nodiscard]] constexpr auto layer() const noexcept -> int;

    constexpr void layer(int layer_no) noexcept;

    [[nodiscard]] constexpr auto position() const noexcept -> std::uint64_t;

    constexpr void position(std::uint64_t position) noexcept;

    [[nodiscard]] constexpr auto parent() const noexcept -> tree_position;
    [[nodiscard]] constexpr auto parent_array_offset() const -> int;

    [[nodiscard]] constexpr auto raw() const noexcept -> storage_type;

    explicit constexpr operator bool() const noexcept;
    auto operator==(tree_position const &other) const noexcept -> bool
            = default;
    auto operator<=>(tree_position const &other) const noexcept
            -> std::strong_ordering
            = default;

private:
    // 8bit layer pos + 56bit sector position on that layer
    storage_type mLayerPosition;
};

constexpr auto next(tree_position position) noexcept -> tree_position;
constexpr auto prev(tree_position position) noexcept -> tree_position;

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
    // computes the path to position from tree_position{0, 6}
    inline tree_path(tree_position position) noexcept;
    // computes the path to position from tree_position{0, treeDepth}
    inline tree_path(int treeDepth, tree_position position) noexcept;

    [[nodiscard]] inline auto layer_position(int layer) const noexcept
            -> tree_position;
    [[nodiscard]] inline auto position(int layer) const noexcept
            -> std::uint64_t;
    [[nodiscard]] inline auto offset(int layer) const noexcept -> int;

    inline explicit operator bool() const noexcept;

    [[nodiscard]] auto begin() const -> const_iterator;
    [[nodiscard]] auto cbegin() const -> const_iterator;
    [[nodiscard]] auto end() const -> const_iterator;
    [[nodiscard]] auto cend() const -> const_iterator;

    [[nodiscard]] auto rbegin() const -> const_reverse_iterator;
    [[nodiscard]] auto crbegin() const -> const_reverse_iterator;
    [[nodiscard]] auto rend() const -> const_reverse_iterator;
    [[nodiscard]] auto crend() const -> const_reverse_iterator;

    [[nodiscard]] inline auto next() const -> tree_path;
    [[nodiscard]] inline auto previous() const -> tree_path;

    [[nodiscard]] inline auto required_depth() const noexcept -> int;

private:
    inline tree_path(int treeDepth, std::uint64_t pos, int layer = 0) noexcept;
    inline tree_path(int treeDepth, int targetLayer) noexcept;

    /**
     * calculate the waypoint offset and absolute value dependent on the layer
     * where it is in and the position
     */
    BOOST_FORCEINLINE auto calc_waypoint_params(int layer, std::uint64_t pos)
            -> waypoint;

    template <int layer>
    void init(std::uint64_t pos) noexcept;

    waypoint_array mTreePath;
    int mTreeDepth;
    int mTargetLayer;
};

class tree_path::iterator
{
    tree_path const *mOwner;
    int mLayer;

public:
    constexpr iterator() noexcept
        : mOwner{nullptr}
        , mLayer{-1}
    {
    }

    using value_type = tree_position;
    using difference_type = int;
    using iterator_category = std::bidirectional_iterator_tag;

    iterator(tree_path const &path)
        : iterator(path, path.mTreeDepth)
    {
    }
    iterator(tree_path const &path, int layer)
        : mOwner(&path)
        , mLayer(layer)
    {
    }

    friend constexpr auto operator==(iterator const &lhs,
                                     iterator const &rhs) noexcept -> bool
    {
        if (lhs.mLayer < 0)
        {
            return rhs.mLayer < 0;
        }
        return lhs.mLayer == rhs.mLayer && lhs.mOwner == rhs.mOwner;
    }

    auto operator*() const noexcept -> value_type
    {
        return mOwner->layer_position(mLayer);
    }

    auto operator++() noexcept -> iterator &
    {
        mLayer -= 1;
        return *this;
    }
    auto operator++(int) noexcept -> iterator
    {
        auto old = *this;
        operator++();
        return old;
    }
    auto operator--() noexcept -> iterator &
    {
        mLayer += 1;
        return *this;
    }
    auto operator--(int) noexcept -> iterator
    {
        auto old = *this;
        operator--();
        return old;
    }

    auto array_offset() -> int
    {
        return mOwner->offset(mLayer);
    }
};
static_assert(std::bidirectional_iterator<tree_path::iterator>);

} // namespace vefs::detail

namespace vefs::detail
{
#pragma region tree_position implementation

/**
 * adds the layer to position. The schema is layer_bits | position_bits.
 */
constexpr auto tree_position::compress(std::uint64_t position, int layer)
        -> storage_type
{
    return (static_cast<storage_type>(layer) << layer_offset)
           | (position & position_mask);
}

constexpr tree_position::tree_position() noexcept
    : mLayerPosition{std::numeric_limits<storage_type>::max()}
{
}
/**
 * sets the current position inside the tree
 * @param position inside the layer
 * @param layer layer number
 */
constexpr tree_position::tree_position(std::uint64_t position,
                                       int layer) noexcept
    : mLayerPosition{compress(position, layer)}
{
}
constexpr tree_position::tree_position(std::uint64_t pos) noexcept
    : tree_position(pos, 0)
{
}

constexpr auto tree_position::layer() const noexcept -> int
{
    return static_cast<int>((mLayerPosition & layer_mask) >> layer_offset);
}

constexpr void tree_position::layer(int layer_no) noexcept
{
    mLayerPosition = (mLayerPosition & position_mask)
                     | static_cast<storage_type>(layer_no) << 56;
}

constexpr auto tree_position::position() const noexcept -> std::uint64_t
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

constexpr auto vefs::detail::tree_position::parent() const noexcept
        -> tree_position
{
    return tree_position{position() / lut::references_per_sector, layer() + 1};
}

/**
 * @return the offset of the parent node within the sector
 */
inline constexpr auto tree_position::parent_array_offset() const -> int
{
    return position() % lut::references_per_sector;
}

constexpr auto tree_position::raw() const noexcept
        -> tree_position::storage_type
{
    return mLayerPosition;
}

constexpr auto next(tree_position value) noexcept -> tree_position
{
    return tree_position(value.position() + 1, value.layer());
}
constexpr auto prev(tree_position value) noexcept -> tree_position
{
    return tree_position(value.position() - 1, value.layer());
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

inline tree_path::tree_path(tree_position position) noexcept
    : tree_path(lut::max_tree_depth + 1, position)
{
}

BOOST_FORCEINLINE auto tree_path::calc_waypoint_params(int layer,
                                                       std::uint64_t pos)
        -> waypoint
{
    auto const absolute = pos / lut::ref_width[layer];
    int const offset = absolute % lut::references_per_sector;
    return {absolute, offset};
}

template <int layer>
inline void tree_path::init(std::uint64_t pos) noexcept
{
    // check sanity of layer
    static_assert(layer <= lut::max_tree_depth);
    static_assert(layer >= 0);
    // the following optimization assumes a maximum tree depth of 4
    static_assert(lut::max_tree_depth == 4);

    // this lets the compiler use compile time divisor lookups
    // which in turn allows for turning the division into a montgomery
    // multiplication. my benchmarks suggest that this is at least twice as fast
    // as a simple loop.

    switch (mTreeDepth)
    {
    case 5:
        mTreePath[4] = calc_waypoint_params(4 - layer, pos);
        [[fallthrough]];

    case 4:
        if constexpr (layer < 4)
        {
            mTreePath[3] = calc_waypoint_params(3 - layer, pos);
        }
        [[fallthrough]];

    case 3:
        if constexpr (layer < 3)
        {
            mTreePath[2] = calc_waypoint_params(2 - layer, pos);
        }
        [[fallthrough]];

    case 2:
        if constexpr (layer < 2)
        {
            mTreePath[1] = calc_waypoint_params(1 - layer, pos);
        }
        [[fallthrough]];

    case 1:
        if constexpr (layer < 1)
        {
            mTreePath[0] = calc_waypoint_params(0, pos);
        }
        [[fallthrough]];

    case 0:
        mTreePath[mTreeDepth].absolute = 0;
        mTreePath[mTreeDepth].offset = 0;
        break;
    }
}

inline tree_path::tree_path(int treeDepth,
                            std::uint64_t pos,
                            int layer) noexcept
    : tree_path(treeDepth, layer)
{
    assert(treeDepth >= 0);
    assert(treeDepth <= lut::max_tree_depth + 1);
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

inline auto tree_path::layer_position(int layer) const noexcept -> tree_position
{
    return {position(layer), layer};
}
inline auto tree_path::position(int layer) const noexcept -> std::uint64_t
{
    return mTreePath[layer].absolute;
}
inline auto tree_path::offset(int layer) const noexcept -> int
{
    return mTreePath[layer].offset;
}

inline tree_path::operator bool() const noexcept
{
    return mTreeDepth >= 0;
}

#pragma endregion

#pragma region tree_path::iterator implementation

inline auto tree_path::begin() const -> const_iterator
{
    return const_iterator{*this};
}

inline auto tree_path::cbegin() const -> const_iterator
{
    return begin();
}

inline auto tree_path::end() const -> const_iterator
{
    return const_iterator{*this, mTargetLayer - 1};
}

inline auto tree_path::cend() const -> const_iterator
{
    return end();
}

inline auto tree_path::rbegin() const -> const_reverse_iterator
{
    return const_reverse_iterator{end()};
}

inline auto tree_path::crbegin() const -> const_reverse_iterator
{
    return rbegin();
}

inline auto tree_path::rend() const -> const_reverse_iterator
{
    return const_reverse_iterator{begin()};
}

inline auto tree_path::crend() const -> const_reverse_iterator
{
    return rend();
}

inline auto tree_path::next() const -> tree_path
{
    return tree_path(mTreeDepth, position(mTargetLayer) + 1, mTargetLayer);
}

inline auto tree_path::previous() const -> tree_path
{
    return tree_path(mTreeDepth, position(mTargetLayer) - 1, mTargetLayer);
}

inline auto tree_path::required_depth() const noexcept -> int
{
    int i = 0;
    while (mTreePath[i].absolute != 0U)
    {
        i += 1;
    }
    return i;
}

#pragma endregion
} // namespace vefs::detail

template <>
struct fmt::formatter<vefs::detail::tree_position>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(vefs::detail::tree_position const &tp, FormatContext &ctx)
    {
        return fmt::format_to(ctx.out(), "(L{}, P{:#04x})", tp.layer(),
                              tp.position());
    }
};

namespace vefs::ed
{
enum class sector_tree_position_tag
{
};
using sector_tree_position
        = error_detail<sector_tree_position_tag, detail::tree_position>;
} // namespace vefs::ed
