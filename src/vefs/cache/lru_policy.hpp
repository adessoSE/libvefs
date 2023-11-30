#pragma once

#include <algorithm>
#include <concepts>
#include <ranges>
#include <span>
#include <vector>

#include <vefs/cache/cache_page.hpp>

namespace vefs::detail
{

template <typename KeyType,
          typename IndexType,
          typename Allocator = std::allocator<void>>
class least_recently_used_policy
{
public:
    using key_type = KeyType;
    using index_type = IndexType;
    using page_state = cache_page_state<KeyType>;
    using allocator_type = typename std::allocator_traits<
            Allocator>::template rebind_alloc<index_type>;
    using list_type = std::vector<index_type, allocator_type>;

private:
    page_state *mPages;
    list_type mLRU;

public:
    least_recently_used_policy(std::span<page_state> pages,
                               std::size_t capacity,
                               Allocator const &alloc = Allocator())
        : mPages(pages.data())
        , mLRU(alloc)
    {
        mLRU.reserve(capacity);
    }

    class replacement_iterator
    {
        friend class least_recently_used_policy;

        least_recently_used_policy *mOwner;
        typename list_type::iterator mHand;

    public:
        constexpr replacement_iterator() noexcept
            : mOwner{}
            , mHand{}
        {
        }

        friend inline auto
        operator==(replacement_iterator const &left,
                   replacement_iterator const &right) noexcept -> bool
                = default;

        explicit replacement_iterator(
                least_recently_used_policy &owner,
                typename list_type::iterator hand) noexcept
            : mOwner(&owner)
            , mHand(std::move(hand))
        {
        }

        using difference_type = std::ptrdiff_t;
        using value_type = page_state;
        using pointer = page_state *;
        using reference = page_state &;
        using iterator_category = std::forward_iterator_tag;

        auto operator*() const noexcept -> reference
        {
            return mOwner->mPages[*mHand];
        }
        auto operator->() const noexcept -> pointer
        {
            return mOwner->mPages + *mHand;
        }

        auto operator++() noexcept -> replacement_iterator &
        {
            ++mHand;
            return *this;
        }
        auto operator++(int) noexcept -> replacement_iterator
        {
            auto old = *this;
            operator++();
            return old;
        }
    };

    [[nodiscard]] auto num_managed() const noexcept -> std::size_t
    {
        return mLRU.size();
    }

    auto begin() -> replacement_iterator
    {
        return replacement_iterator{*this, mLRU.begin()};
    }
    auto end() -> replacement_iterator
    {
        return replacement_iterator(*this, mLRU.end());
    }

    void insert(key_type const &, index_type where) noexcept
    {
        mLRU.push_back(where);
    }

    auto on_access(key_type const &, index_type where) noexcept -> bool
    {
        auto const it = std::ranges::find(mLRU, where);
        auto const end = mLRU.end();
        if (it == end)
        {
            return false;
        }

        std::ranges::rotate(it, std::next(it), end);
        return true;
    }

    auto try_evict(replacement_iterator &&which,
                   index_type &where,
                   typename page_state::state_type &generation) noexcept
            -> cache_replacement_result
    {
        auto const rx = which->try_start_replace(generation);
        if (rx != cache_replacement_result::pinned)
        {
            where = *which.mHand;
            mLRU.erase(which.mHand);
        }
        return rx;
    }
    auto on_purge(key_type const &, index_type where) noexcept -> bool
    {
        auto const it = std::ranges::find(mLRU, where);
        if (it != mLRU.end())
        {
            mLRU.erase(it);
            return true;
        }

        return false;
    }
};

} // namespace vefs::detail
