#pragma once

#include <cstdint>

#include <map>
#include <vector>
#include <optional>

#include <vefs/exceptions.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/bitset_overlay.hpp>

namespace vefs::utils
{
    template <typename IdType>
    class block_manager
    {
    public:
        using id_type = IdType;
        using block_map = std::map<id_type, std::uint64_t>;

        auto alloc_one() -> std::optional<id_type>;
        auto alloc_multiple(std::uint64_t num) -> std::optional<std::vector<id_type>>;

        auto alloc_consecutive(std::uint64_t num)
            -> std::optional<id_type>;
        // [begin, end] -> [begin, end + numRequested] | [begin - numRequested, end]
        auto try_extend(id_type begin, id_type end, std::uint64_t numRequested) -> int;

        void dealloc(id_type one);
        void dealloc(std::vector<id_type> blocks);
        void dealloc(id_type first, std::uint64_t num);

        void write_to_bitset(bitset_overlay data, const IdType begin,
            const std::size_t num) const;

    private:
        void dealloc_range(id_type rLastId, std::uint64_t rNumPrev);
        void remove_from(typename block_map::iterator where, std::uint64_t offset);

        block_map mFreeMap;
    };


    template <typename IdType>
    inline void parse_bitset(const IdType begin, const const_bitset_overlay data,
        const std::size_t num, block_manager<IdType> &target)
    {
        auto current = begin;
        std::uint64_t num = 0;
        for (auto i = 0; i < num; ++i)
        {
            if (data[i])
            {
                if (num == 0)
                {
                    current = begin + i;
                }
                num += 1;
            }
            else if (num != 0)
            {
                target.dealloc(current, num);
                num = 0;
            }
        }

        if (num != 0)
        {
            target.dealloc(current, num);
        }
    }

    template <typename IdType>
    inline void block_manager<IdType>::write_to_bitset(bitset_overlay data, const IdType begin,
        const std::size_t num) const
    {
        data.set_n(num);

        // [begin, end]
        const auto end = begin + num - 1;

        auto it = mFreeMap.lower_bound(begin);
        if (it == mFreeMap.end())
        {
            return;
        }

        // [it, iend)
        auto iend = mFreeMap.upper_bound(end);
        if (iend != mFreeMap.end() && std::get<0>(*iend) - std::get<1>(*iend) <= end)
        {
            ++iend;
        }

        for (; it != iend; ++it)
        {
            auto[cEnd, cOff] = *it;
            auto rBegin = cEnd - cOff;

            rBegin = std::max(rBegin, begin) - begin;
            const auto rEnd = std::min(cEnd, end) - begin;

            for (; rBegin <= rEnd; ++rBegin)
            {
                data.unset(rBegin);
            }
        }
    }


    template <typename IdType>
    inline void block_manager<IdType>::remove_from(typename block_map::iterator where,
        std::uint64_t offset)
    {
        if (offset == std::get<1>(*where))
        {
            mFreeMap.erase(where);
        }
        else
        {
            where->second -= offset;
        }
    }

    template <typename IdType>
    inline auto block_manager<IdType>::alloc_one()
        -> std::optional<id_type>
    {
        if (mFreeMap.empty())
        {
            return std::nullopt;
        }

        auto it = mFreeMap.begin();
        auto &[lastId, numPrev] = *it;
        std::optional<id_type> result = lastId - numPrev;

        remove_from(it, 0);

        return result;
    }

    template <typename IdType>
    inline auto block_manager<IdType>::alloc_multiple(std::uint64_t num)
        -> std::optional<std::vector<id_type>>
    {
        if (mFreeMap.empty())
        {
            return std::nullopt;
        }

        std::vector<id_type> allocated;
        allocated.reserve(num);

        using action_queue = std::vector<block_map::iterator>;
        action_queue removedRanges;
        removedRanges.reserve(num);

        auto it = mFreeMap.begin();
        const auto end = mFreeMap.end();

        while (num)
        {
            if (it == end)
            {
                return std::nullopt;
            }

            auto &[lastId, numPrev] = *it;

            std::uint64_t i = 0;
            for (; num && i <= numPrev; ++i)
            {
                allocated.push_back(lastId - numPrev + i);
                --num;
            }

            if (i > numPrev)
            {
                removedRanges.push_back(it);
            }
            else
            {
                // this only happens if we leave the, i.e. num == 0
                numPrev -= i;
            }
        }

        for (const auto &marked : removedRanges)
        {
            mFreeMap.erase(marked);
        }

        return allocated;
    }

    template <typename IdType>
    inline auto block_manager<IdType>::alloc_consecutive(std::uint64_t num)
        -> std::optional<id_type>
    {
        if (num == 0)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{});
        }
        if (mFreeMap.empty())
        {
            return std::nullopt;
        }

        num -= 1;
        for (auto it = mFreeMap.begin(), end = mFreeMap.end(); it != end; ++it)
        {
            auto &[lastId, numPrev] = *it;
            if (num <= numPrev)
            {
                std::optional<id_type> result{ lastId - numPrev };

                remove_from(it, num);

                if (num == numPrev)
                {
                    mFreeMap.erase(lastId);
                }
                else
                {
                    numPrev -= num;
                }

                return result;
            }
        }

        return std::nullopt;
    }

    template <typename IdType>
    inline auto block_manager<IdType>::try_extend(id_type begin, id_type end, std::uint64_t numRequested)
        -> int
    {
        if (numRequested == 0)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{});
        }
        if (mFreeMap.empty())
        {
            return 0;
        }
        numRequested -= 1;

        auto it = mFreeMap.upper_bound(begin);
        const auto itEnd = mFreeMap.end();
        if (it != itEnd)
        {
            auto &[lastId, numPrev] = *it;
            if (numRequested <= numPrev && lastId - numPrev - 1 == end)
            {
                remove_from(it, numRequested);
                return 1;
            }
        }

        // failed to extend after end, so try to extend before begin
        if (it != mFreeMap.begin())
        {
            --it;

            auto &[lastId, numPrev] = *it;
            if (numRequested <= numPrev && lastId + 1 == begin)
            {
                remove_from(it, numRequested);
                return -1;
            }
        }
        return 0;
    }

    template <typename IdType>
    inline void block_manager<IdType>::dealloc(id_type one)
    {
        dealloc_range(one, 0);
    }

    template <typename IdType>
    inline void block_manager<IdType>::dealloc(std::vector<id_type> blocks)
    {
        if (blocks.empty())
        {
            return;
        }

        std::sort(blocks.begin(), blocks.end());
        auto newEnd = std::unique(blocks.begin(), blocks.end());
        blocks.resize(std::distance(blocks.begin(), newEnd));


        // #TODO implement a less wasteful rollback strategy
        auto backup = mFreeMap;
        VEFS_ERROR_EXIT{
            mFreeMap = std::move(backup);
        };

        auto current = blocks.front();
        auto offset = 0ull;
        for (auto it = ++blocks.cbegin(), end = blocks.cend(); it != end; ++it)
        {
            const auto next = *it;
            if (static_cast<std::uint64_t>(next) - static_cast<std::uint64_t>(current) == 1)
            {
                ++offset;
            }
            else
            {
                dealloc_range(current, offset);
                offset = 0;
            }
            current = next;
        }
        dealloc_range(current, offset);
    }

    template <typename IdType>
    inline void block_manager<IdType>::dealloc(id_type first, std::uint64_t num)
    {
        if (num == 0)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{});
        }

        num -= 1;
        dealloc_range(first + num, num);
    }

    template <typename IdType>
    inline void block_manager<IdType>::dealloc_range(id_type rLastId, std::uint64_t rNumPrev)
    {
        if (mFreeMap.empty())
        {
            mFreeMap.emplace(rLastId, rNumPrev);
            return;
        }

        auto rBeginId = rLastId - rNumPrev;

        auto it = mFreeMap.upper_bound(rLastId);
        if (it != mFreeMap.end())
        {
            auto &[lastId, numPrev] = *it;
            if (lastId - numPrev - 1 == rLastId)
            {
                // freed range is just before the next free range
                numPrev += rNumPrev + 1;

                if (it != mFreeMap.begin())
                {
                    auto pit = it;
                    --pit;
                    if (std::get<0>(*pit) + 1 == rBeginId)
                    {
                        numPrev += std::get<1>(*pit) + 1;
                        mFreeMap.erase(pit);
                    }
                }
                return;
            }
        }

        if (it != mFreeMap.begin())
        {
            auto pit = it;
            --pit;

            if (std::get<0>(*pit) + 1 == rBeginId)
            {
                // a range before one can be extended
                mFreeMap.emplace_hint(it, rLastId, std::get<1>(*pit) + rNumPrev + 1);
                mFreeMap.erase(pit);

                return;
            }
        }

        // couldn't put it in an existing entry, so we create a new one
        mFreeMap.emplace_hint(it, rLastId, rNumPrev);
    }
}
