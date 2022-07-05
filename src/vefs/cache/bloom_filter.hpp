#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <dplx/cncr/math_supplement.hpp>

#include <vefs/hash/hash_algorithm.hpp>
#include <vefs/hash/spooky_v2.hpp>

namespace vefs::detail
{

/**
 * @brief A conventional bloom filter is a probablistic data structure for
 *        checking set membership.
 *
 * @tparam T is the type of the tracked objects. Must be hashable with
 *           @ref spooky_v2_hash
 * @tparam Allocator to be used for the hash buckets.
 */
template <hashable<spooky_v2_hash> T, typename Allocator = std::allocator<void>>
class bloom_filter
{
public:
    using value_type = T;
    using size_type = std::uint32_t;
    using hasher = spooky_v2_hash;
    using bucket_type = std::size_t;

    /**
     * @brief The number of hash functions.
     *
     * The current implementation is optimized by taking the 128bit hash output
     * and splitting it into four 32bit parts.
     */
    static constexpr unsigned k = 4U;

    static constexpr unsigned bits_per_cell = 1U;
    static constexpr unsigned cells_per_bucket
            = std::numeric_limits<bucket_type>::digits / bits_per_cell;
    static constexpr unsigned cell_limit = 1U << bits_per_cell;
    static constexpr unsigned cell_mask = cell_limit - 1U;

private:
    using allocator_type = typename std::allocator_traits<
            Allocator>::template rebind_alloc<bucket_type>;

    using container_type = std::vector<bucket_type, allocator_type>;

    container_type mBuckets;

public:
    constexpr bloom_filter() noexcept = default;

    bloom_filter(std::uint32_t const numCells,
                 allocator_type const &allocator = allocator_type())
        : mBuckets(dplx::cncr::round_up_p2(numCells / cells_per_bucket,
                                           64U / sizeof(bucket_type)),
                   allocator)
    {
    }

    friend inline void swap(bloom_filter &left, bloom_filter &right) noexcept
    {
        using std::swap;
        swap(left.mBuckets, right.mBuckets);
    }

    /**
     * @brief returns the number of counters.
     */
    auto num_cells() const noexcept -> size_type
    {
        return static_cast<size_type>(mBuckets.size()) * cells_per_bucket;
    }

    /**
     * @brief Returns zero if the object definitely is not part of the set,
     *        otherwise returns one.
     */
    auto estimate(T const &value) const noexcept -> std::uint32_t
    {
        auto const hashes = std::bit_cast<std::array<std::uint32_t, k>>(
                hash<hasher, hash128_t>(value));

        unsigned estimate = cell_mask;
        for (auto const h : hashes)
        {
            auto const cellIndex = detail::hash_to_index(h, num_cells());
            auto const cellShift
                    = (cellIndex % cells_per_bucket) * bits_per_cell;
            auto const bucketIndex = cellIndex / cells_per_bucket;

            auto const cell = (mBuckets[bucketIndex] >> cellShift) & cell_mask;
            estimate &= cell;
        }
        return estimate;
    }

    /**
     * @brief Add an item to the frequency sketch.
     * @return true if the item has been added, false if all counters reached
     *         their max value.
     */
    auto observe(T const &value) noexcept -> bool
    {
        auto const hashes = std::bit_cast<std::array<std::uint32_t, k>>(
                hash<hasher, hash128_t>(value));

        unsigned estimate = cell_mask;
        for (auto h : hashes)
        {
            auto const cellIndex = detail::hash_to_index(h, num_cells());
            auto const cellShift
                    = (cellIndex % cells_per_bucket) * bits_per_cell;
            auto const bucketIndex = cellIndex / cells_per_bucket;

            estimate &= (mBuckets[bucketIndex] >> cellShift) & cell_mask;
            mBuckets[bucketIndex] |= static_cast<bucket_type>(1) << cellShift;
        }
        return estimate == 0U;
    }

    /**
     * @brief Resets the bloom filter, i.e. \ref estimate returns zero for all
     *        objects.
     */
    void reset() noexcept
    {
        std::memset(mBuckets.data(), 0, mBuckets.size() * sizeof(bucket_type));
    }
};

} // namespace vefs::detail
