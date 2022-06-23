#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <boost/config.hpp>

#include <dplx/cncr/math_supplement.hpp>

#include <vefs/hash/hash_algorithm.hpp>
#include <vefs/hash/spooky_v2.hpp>

namespace vefs
{

template <hashable<spooky_v2_hash> T, typename Allocator = std::allocator<void>>
class spectral_bloom_filter
{
    using bucket_type = std::size_t;
    using allocator_type = std::allocator_traits<
            Allocator>::template rebind_alloc<bucket_type>;

    using hasher = spooky_v2_hash;
    using bucket_traits = std::allocator_traits<allocator_type>;
    static_assert(
            std::same_as<bucket_type, typename bucket_traits::value_type>);

    bucket_type *mBuckets;
    std::uint32_t mNumCells;
    std::uint32_t mSamples;
    [[no_unique_address]] allocator_type mAllocator;

public:
    ~spectral_bloom_filter() noexcept
    {
        if (mBuckets != nullptr)
        {
            bucket_traits::deallocate(mAllocator, mBuckets, num_buckets());
        }
    }
    spectral_bloom_filter() noexcept
        : mBuckets{}
        , mNumCells{}
        , mSamples{}
        , mAllocator()
    {
    }

    using value_type = T;

    static constexpr unsigned k = 4U;

    static constexpr unsigned bits_per_cell = 4U;
    static constexpr unsigned cells_per_bucket
            = std::numeric_limits<bucket_type>::digits / bits_per_cell;
    static constexpr unsigned cell_limit = 1U << bits_per_cell;
    static constexpr unsigned cell_mask = cell_limit - 1U;
    static constexpr unsigned cell_reset_mask = cell_mask >> 1;
    static constexpr bucket_type bucket_reset_mask
            = static_cast<bucket_type>(0x7777'7777'7777'7777ULL);
    static constexpr bucket_type bucket_oddity_mask
            = static_cast<bucket_type>(0x1111'1111'1111'1111ULL);

    spectral_bloom_filter(spectral_bloom_filter const &other)
        : mBuckets{}
        , mNumCells{other.mNumCells}
        , mSamples{other.mSamples}
        , mAllocator(bucket_traits::select_on_container_copy_construction(
                  other.mAllocator))
    {
        if (mNumCells == 0U)
        {
            return;
        }
        mBuckets = bucket_traits::allocate(mAllocator, num_buckets());
        if (mBuckets != nullptr)
        {
            std::memcpy(mBuckets, other.mBuckets,
                        num_buckets() * sizeof(bucket_type));
        }
    }
    auto operator=(spectral_bloom_filter const &other)
            -> spectral_bloom_filter &
    {
        if (mBuckets)
        {
            bucket_traits::deallocate(mAllocator,
                                      std::exchange(mBuckets, nullptr),
                                      num_buckets());
        }
        mNumCells = other.mNumCells;
        mSamples = other.mSamples;
        if constexpr (bucket_traits::propagate_on_container_copy_assignment::
                              value)
        {
            mAllocator = other.mAllocator;
        }

        if (mNumCells > 0U)
        {
            mBuckets = bucket_traits::allocate(mAllocator, num_buckets());
            if (mBuckets != nullptr)
            {
                std::memcpy(mBuckets, other.mBuckets,
                            num_buckets() * sizeof(bucket_type));
            }
        }

        return *this;
    }

    spectral_bloom_filter(spectral_bloom_filter &&other) noexcept
        : mBuckets{std::exchange(other.mBuckets, nullptr)}
        , mNumCells{std::exchange(other.mNumCells, 0U)}
        , mSamples{std::exchange(other.mSamples, 0U)}
        , mAllocator(std::move(other.mAllocator))
    {
    }
    auto operator=(spectral_bloom_filter &&other) noexcept
            -> spectral_bloom_filter &
    {
        if (mBuckets != nullptr)
        {
            bucket_traits::deallocate(mAllocator, mBuckets, mNumCells);
        }
        mBuckets = std::exchange(other.mBuckets, nullptr);
        mNumCells = std::exchange(other.mNumCells, 0U);
        mSamples = std::exchange(other.mSamples, 0U);
        if constexpr (bucket_traits::propagate_on_container_move_assignment::
                              value)
        {
            mAllocator = std::move(mAllocator);
        }
        return *this;
    }

    explicit spectral_bloom_filter(std::uint32_t numCells,
                                   Allocator const &allocator = {})
        : mBuckets{}
        , mNumCells{dplx::cncr::round_up_p2(numCells, 64U * cells_per_bucket)}
        , mSamples{}
        , mAllocator{allocator}
    {
        if (mNumCells == 0U || mNumCells < numCells)
        {
            mNumCells = 0U;
            return;
        }
        mBuckets = bucket_traits::allocate(mAllocator, num_buckets());
        std::memset(mBuckets, 0, num_buckets() * sizeof(bucket_type));
    }

    friend inline void swap(spectral_bloom_filter &left,
                            spectral_bloom_filter &right) noexcept
    {
        using std::swap;
        swap(left.mBuckets, right.mBuckets);
        swap(left.mNumCells, right.mNumCells);
        swap(left.mSamples, right.mSamples);
        swap(left.mAllocator, right.mAllocator);
    }

    auto num_cells() const noexcept -> std::uint32_t
    {
        return mNumCells;
    }
    auto samples() const noexcept -> std::uint32_t
    {
        return mSamples;
    }
    auto max_samples() const noexcept -> std::uint32_t
    {
        return mNumCells / 2U;
    }

    auto estimate(T const &value) const noexcept -> std::uint32_t
    {
        auto const hashes = std::bit_cast<std::array<std::uint32_t, k>>(
                hash<hasher, hash128_t>(value));

        unsigned estimate = cell_limit;
        for (unsigned i = 0; i < k; ++i)
        {
            auto const cellIndex = hash_to_index(hashes[i]);
            auto const cellShift
                    = (cellIndex % cells_per_bucket) * bits_per_cell;
            auto const bucketIndex = cellIndex / cells_per_bucket;

            auto const value = (mBuckets[bucketIndex] >> cellShift) & cell_mask;

            if (value < estimate)
            {
                estimate = value;
            }
        }
        return estimate;
    }

    void observe(T const &value) noexcept
    {
        auto const hashes = std::bit_cast<std::array<std::uint32_t, k>>(
                hash<hasher, hash128_t>(value));

        unsigned cellShifts[k];
        std::uint32_t bucketIndices[k];
        unsigned values[k];

        for (unsigned i = 0U; i < k; ++i)
        {
            auto const index = hash_to_index(hashes[i]);
            cellShifts[i] = (index % cells_per_bucket) * bits_per_cell;
            bucketIndices[i] = index / cells_per_bucket;

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
            return;
        }

        unsigned samples = 0U;
        for (unsigned i = 0U; i < k; ++i)
        {
            unsigned const isSample = values[i] == estimate;
            samples += isSample;
            mBuckets[bucketIndices[i]] += static_cast<bucket_type>(isSample)
                                       << cellShifts[i];
        }

        mSamples += samples;
        if (mSamples >= max_samples())
        {
            reset();
        }
    }

private:
    void reset() noexcept
    {
        std::uint32_t truncationCounter = 0U;
        for (std::uint32_t i = 0, numBuckets = num_buckets(); i < numBuckets;
             ++i)
        {
            // count the odd numbers which will be truncated and therefore need
            // to be subtracted from the sample size
            truncationCounter += static_cast<std::uint32_t>(
                    std::popcount(mBuckets[i] & bucket_oddity_mask));
            mBuckets[i] >>= 1;
            mBuckets[i] &= bucket_reset_mask;
        }
        mSamples = (mSamples - truncationCounter) / 2U;
    }

    auto num_buckets() const noexcept -> std::uint32_t
    {
        return mNumCells / cells_per_bucket;
    }
    auto hash_to_index(std::uint32_t hv) const noexcept -> std::uint32_t
    {
        // https://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction/
        return static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(hv) * mNumCells) >> 32);
    }
};

} // namespace vefs
