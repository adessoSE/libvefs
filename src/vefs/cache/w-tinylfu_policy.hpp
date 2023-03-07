#pragma once

#include <memory>

#include <vefs/cache/bloom_filter.hpp>
#include <vefs/cache/cache_page.hpp>
#include <vefs/cache/lru_policy.hpp>
#include <vefs/cache/slru_policy.hpp>
#include <vefs/cache/spectral_bloom_filter.hpp>

namespace vefs::detail
{

template <typename KeyType,
          typename IndexType,
          typename Allocator = std::allocator<void>>
class wtinylfu_policy
{
public:
    using key_type = KeyType;
    using index_type = IndexType;
    using page_state = cache_page_state<KeyType>;
    using allocator_type = typename std::allocator_traits<
            Allocator>::template rebind_alloc<index_type>;
    using window_policy_type
            = least_recently_used_policy<key_type, index_type, Allocator>;
    using main_policy_type = segmented_least_recently_used_policy<key_type,
                                                                  index_type,
                                                                  Allocator>;
    using doorkeeper_type = bloom_filter<key_type, Allocator>;
    using frequency_sketch_type = spectral_bloom_filter<key_type, Allocator>;

private:
    page_state *mPages;
    std::size_t mWindowSize;
    window_policy_type mWindowPolicy;
    main_policy_type mMainPolicy;
    doorkeeper_type mDoorkeeper;
    frequency_sketch_type mFrequencySketch;
    unsigned mSamples;
    unsigned mMaxSamples;

    static constexpr std::size_t divider = 100U;

public:
    explicit wtinylfu_policy(std::span<page_state> const pages,
                             std::size_t const capacity,
                             Allocator const &alloc = Allocator())
        : mPages(pages.data())
        , mWindowSize(std::max<std::size_t>(capacity / divider, 2U))
        , mWindowPolicy(pages, mWindowSize, alloc)
        , mMainPolicy(pages, capacity - mWindowSize, alloc)
        , mDoorkeeper(capacity, alloc)
        , mFrequencySketch(capacity, alloc)
        , mSamples{}
        , mMaxSamples{static_cast<unsigned>(capacity * 16U)} // W/C = 4Bit ctrs
    {
    }

    class replacement_iterator
    {
        friend class wtinylfu_policy;

        wtinylfu_policy *mOwner;
        typename window_policy_type::replacement_iterator mWindowHand;
        typename main_policy_type::replacement_iterator mMainHand;
        bool mFromWindow;

    public:
        constexpr replacement_iterator()
            : mOwner{}
            , mWindowHand{}
            , mMainHand{}
            , mFromWindow{}
        {
        }

        friend inline auto
        operator==(replacement_iterator const &left,
                   replacement_iterator const &right) noexcept -> bool
        {
            return left.mOwner == right.mOwner
                && left.mWindowHand == right.mWindowHand
                && left.mMainHand == right.mMainHand;
        }

        // constructs an end iterator
        explicit replacement_iterator(wtinylfu_policy &owner)
            : mOwner{&owner}
            , mWindowHand{owner.mWindowPolicy.end()}
            , mMainHand{owner.mMainPolicy.end()}
            , mFromWindow{}
        {
        }
        // expects windowHand and mainHand not to be end iterators
        explicit replacement_iterator(
                wtinylfu_policy &owner,
                typename window_policy_type::replacement_iterator windowHand,
                typename main_policy_type::replacement_iterator mainHand)
            : mOwner{&owner}
            , mWindowHand{windowHand}
            , mMainHand{mainHand}
            , mFromWindow{}
        {
            if (auto const mainEnd = owner.mMainPolicy.end();
                mWindowHand != owner.mWindowPolicy.end()
                && mMainHand != mainEnd)
            {
                mFromWindow = should_use_window(mWindowHand, mMainHand);
            }
            else if (mMainHand == mainEnd)
            {
                mFromWindow = true;
            }
        }

        using difference_type = std::ptrdiff_t;
        using value_type = page_state;
        using pointer = page_state *;
        using reference = page_state &;
        using iterator_category = std::forward_iterator_tag;

        auto operator*() const noexcept -> reference
        {
            return mFromWindow ? mWindowHand.operator*()
                               : mMainHand.operator*();
        }
        auto operator->() const noexcept -> pointer
        {
            return mFromWindow ? mWindowHand.operator->()
                               : mMainHand.operator->();
        }

        auto operator++() noexcept -> replacement_iterator &
        {
            auto const endWindow = mOwner->mWindowPolicy.end();
            auto const endMain = mOwner->mMainPolicy.end();
            if (mMainHand == endMain)
            {
                ++mWindowHand;
                return *this;
            }
            else if (mWindowHand == endWindow)
            {
                ++mMainHand;
                return *this;
            }

            auto const nextWindow = std::next(mWindowHand);
            auto const nextMain = std::next(mMainHand);
            if ((mFromWindow || nextMain == endMain) && nextWindow != endWindow)
            {
                mWindowHand = nextWindow;
            }
            else if (nextMain != endMain)
            {
                mMainHand = nextMain;
            }
            else
            {
                if (mFromWindow)
                {
                    mWindowHand = nextWindow;
                }
                else
                {
                    mMainHand = nextMain;
                }
                mFromWindow = !mFromWindow;
                return *this;
            }
            mFromWindow = should_use_window(mWindowHand, mMainHand);
            return *this;
        }
        auto operator++(int) noexcept -> replacement_iterator
        {
            auto old = *this;
            operator++();
            return old;
        }

    private:
        auto should_use_window(
                typename window_policy_type::replacement_iterator const
                        &windowHand,
                typename main_policy_type::replacement_iterator const &mainHand)
                const noexcept -> bool
        {
            return mOwner->estimate(windowHand->key())
                <= mOwner->estimate(mainHand->key());
        }
    };

    auto num_managed() const noexcept -> std::size_t
    {
        return mWindowPolicy.num_managed() + mMainPolicy.num_managed();
    }

    auto begin() noexcept -> replacement_iterator
    {
        return replacement_iterator{*this, mWindowPolicy.begin(),
                                    mMainPolicy.begin()};
    }
    auto end() noexcept -> replacement_iterator
    {
        return replacement_iterator{*this};
    }

    void insert(key_type const &key, index_type const where) noexcept
    {
        if (mWindowPolicy.num_managed() == mWindowSize)
        {
            // migrate the non evicted entry from the window to the main cache
            auto const wndKey = mWindowPolicy.begin().operator->();
            auto const wndIdx = wndKey - mPages;
            mWindowPolicy.on_purge(wndKey->key(), wndIdx);
            mMainPolicy.insert(wndKey->key(), wndIdx);
        }
        mWindowPolicy.insert(key, where);
    }
    auto on_access(key_type const &key, index_type const where) noexcept -> bool
    {
        if (!mWindowPolicy.on_access(key, where)
            && !mMainPolicy.on_access(key, where))
        {
            return false;
        }
        if (!mDoorkeeper.observe(key))
        {
            if (!mFrequencySketch.observe(key))
            {
                return true;
            }
        }
        mSamples += 1U;
        if (mSamples == mMaxSamples)
        {
            mSamples /= 2U;
            mDoorkeeper.reset();
            mFrequencySketch.reset();
        }
        return true;
    }

    auto try_evict(replacement_iterator &&which,
                   index_type &where,
                   typename page_state::state_type &nextGeneration) noexcept
            -> cache_replacement_result
    {
        cache_replacement_result rx;
        if (which.mFromWindow)
        {
            rx = mWindowPolicy.try_evict(std::move(which.mWindowHand), where,
                                         nextGeneration);
            if (rx == cache_replacement_result::pinned)
            {
                return rx;
            }
        }
        else
        {
            rx = mMainPolicy.try_evict(std::move(which.mMainHand), where,
                                       nextGeneration);
            if (rx == cache_replacement_result::pinned)
            {
                return rx;
            }
            // migrate the non evicted entry from the window to the main cache
            auto const wndKey = which.mWindowHand.operator->();
            auto const wndIdx = wndKey - mPages;
            mWindowPolicy.on_purge(wndKey->key(), wndIdx);
            mMainPolicy.insert(wndKey->key(), wndIdx);
        }
        return rx;
    }
    auto on_purge(key_type const &key, index_type const where) noexcept -> bool
    {
        return mWindowPolicy.on_purge(key, where)
            || mMainPolicy.on_purge(key, where);
    }

private:
    auto estimate(key_type const &key) const noexcept -> std::uint32_t
    {
        return mDoorkeeper.estimate(key) > 0U
                     ? 1U + mFrequencySketch.estimate(key)
                     : 1U;
    }
};

} // namespace vefs::detail
