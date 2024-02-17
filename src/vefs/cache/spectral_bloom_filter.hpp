#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <boost/predef/compiler.h>
#include <dplx/cncr/math_supplement.hpp>

#include <vefs/hash/hash_algorithm.hpp>
#include <vefs/hash/spooky_v2.hpp>

namespace vefs::detail
{

/**
 * @brief A spectral bloom filter (with conservative update) is a frequency
 *        sketch for objects.
 *
 * A spectral bloom filter "is a hash-based data structure to represent a
 * dynamically changing associative array of counters." [1] This implementation
 * utilizes a technique known as conservative update [1] which only increments
 * the minimal counters associated with an object.
 *
 * The core methods are @ref observe() and @ref estimate() which add an item to
 * the data structure and how often it has been observed respectively.
 * Additionally we implement the reset mechanic detailed in [2].
 *
 * @tparam T is the type of the tracked objects. Must be hashable with
 *           @ref spooky_v2_hash
 * @tparam Allocator to be used for the hash buckets.
 *
 * @see [1]: https://arxiv.org/pdf/2203.15496.pdf
 * @see [2]: https://arxiv.org/pdf/1512.00727.pdf
 */
template <hashable<spooky_v2_hash> T, typename Allocator = std::allocator<void>>
class spectral_bloom_filter
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

    static constexpr unsigned bits_per_cell = 4U;
    static constexpr unsigned cells_per_bucket
            = std::numeric_limits<bucket_type>::digits / bits_per_cell;
    static constexpr unsigned cell_limit = 1U << bits_per_cell;
    static constexpr unsigned cell_mask = cell_limit - 1U;
    static constexpr unsigned cell_reset_mask = cell_mask >> 1;
#if defined(BOOST_COMP_GNUC_AVAILABLE) && !defined(BOOST_COMP_GNUC_EMULATED)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#endif
    static constexpr bucket_type bucket_reset_mask
            = static_cast<bucket_type>(0x7777'7777'7777'7777U);
    static constexpr bucket_type bucket_oddity_mask
            = static_cast<bucket_type>(0x1111'1111'1111'1111U);
#if defined(BOOST_COMP_GNUC_AVAILABLE) && !defined(BOOST_COMP_GNUC_EMULATED)
#pragma GCC diagnostic pop
#endif

private:
    using allocator_type = typename std::allocator_traits<
            Allocator>::template rebind_alloc<bucket_type>;

    using container_type = std::vector<bucket_type, allocator_type>;

    container_type mBuckets;

public:
    constexpr spectral_bloom_filter() noexcept = default;

    explicit spectral_bloom_filter(size_type const numCells,
                                   allocator_type const &allocator
                                   = allocator_type())
        : mBuckets(dplx::cncr::round_up_p2(numCells / cells_per_bucket,
                                           64U / sizeof(bucket_type)),
                   allocator)
    {
    }

    friend inline void swap(spectral_bloom_filter &left,
                            spectral_bloom_filter &right) noexcept
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
     * @brief Estimates the frequency of the given object.
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

            estimate = cell < estimate ? cell : estimate;
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

        unsigned cellShifts[k];
        std::uint32_t bucketIndices[k];
        unsigned values[k];

        for (unsigned i = 0U; i < k; ++i)
        {
            auto const cellIndex
                    = detail::hash_to_index(hashes[i], num_cells());
            cellShifts[i] = (cellIndex % cells_per_bucket) * bits_per_cell;
            bucketIndices[i] = cellIndex / cells_per_bucket;

            values[i]
                    = (mBuckets[bucketIndices[i]] >> cellShifts[i]) & cell_mask;
        }

        unsigned estimate = values[0];
        for (unsigned i = 1U; i < k; ++i)
        {
            if (estimate > values[i])
            {
                estimate = values[i];
            }
        }
        if (estimate == cell_mask)
        {
            return false;
        }

        for (unsigned i = 0U; i < k; ++i)
        {
            unsigned const incr = values[i] == estimate;
            mBuckets[bucketIndices[i]] += static_cast<bucket_type>(incr)
                                       << cellShifts[i];
        }
        return true;
    }

    /**
     * @brief Implements an aging mechanic by halving all counter values.
     * @return The number of odd counters i.e. the truncation error sum.
     */
    auto reset() noexcept -> std::uint32_t
    {
        std::uint32_t truncationCounter = 0U;
        for (auto &bucket : mBuckets)
        {
            // count the odd numbers which will be truncated and therefore need
            // to be subtracted from the sample size
            truncationCounter += static_cast<std::uint32_t>(
                    std::popcount(bucket & bucket_oddity_mask));
            bucket >>= 1;
            bucket &= bucket_reset_mask;
        }
        return truncationCounter;
    }
};

} // namespace vefs::detail
