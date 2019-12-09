#pragma once

#include <cstdint>

#include <algorithm>
#include <memory_resource>
#include <numeric>
#include <utility>

#include <boost/intrusive/avl_set.hpp>

#include <vefs/disappointment.hpp>
#include <vefs/utils/bitset_overlay.hpp>
#include <vefs/utils/misc.hpp>

namespace vefs::utils
{
    /**
     * Represents a contiguous numeric id range as [firstId, lastId]
     */
    template <typename IdType>
    class id_range final
        : public boost::intrusive::avl_set_base_hook<boost::intrusive::optimize_size<true>>
    {
        template <typename T>
        static constexpr auto xderive_type(T) noexcept
        {
            if constexpr (std::is_enum_v<T>)
            {
                return std::underlying_type_t<T>{};
            }
            else
            {
                return T{};
            }
        }

    public:
        /**
         * the id type used in the public interface
         */
        using id_type = IdType;
        /**
         * the type used for computations; either the id_type itself or its std::underlying_type if
         * id_type is an enum.
         */
        using underlying_type = decltype(xderive_type(id_type{}));
        /**
         * the type used for representing the distance between two ids
         */
        using difference_type = std::make_signed_t<underlying_type>;

        // [first, last]
        id_range(id_type first, id_type last) noexcept;

        /**
         * computes id + num in a type safe fashion (i.e. accounts for enum types)
         */
        static auto advance(id_type id, difference_type num) noexcept -> id_type;
        /**
         * computes to - from in a typesafe fashion (i.e. accounts for enum types)
         */
        static auto distance(id_type from, id_type to) noexcept -> difference_type;

        /**
         * the identifier used for ordering id_ranges
         *
         * currently the ranges are ordered by their last id
         */
        auto id() const noexcept -> id_type;
        /**
         * the first id within the range
         */
        auto first() const noexcept -> id_type;
        /**
         * the last id within the range
         */
        auto last() const noexcept -> id_type;

        /**
         * returns the first id from the range and removes it from the range
         *
         * \precondition !empty()
         */
        auto pop_front() noexcept -> id_type;
        /**
         * returns the first id from the range and removes num ids from the front of the range
         * effectively allocating a contiguous range
         *
         * \precondition size() >= num
         */
        auto pop_front(std::size_t num) noexcept -> id_type;
        /**
         * fills the ids span with as many ids as possible and removes the used ids from the range
         */
        auto pop_front(span<id_type> ids) noexcept -> std::size_t;
        /**
         * removes num ids from the back of the range and returns the smallest removed id,
         * effectively allocating a contiguous range starting at the returned id
         */
        auto pop_back(std::size_t num) noexcept -> id_type;

        /**
         * prepends num ids to the range
         */
        void prepend(std::size_t num) noexcept;
        /**
         * appends num ids to the range
         */
        void append(std::size_t num) noexcept;

        auto empty() noexcept -> bool;
        auto size() const noexcept -> std::size_t;

        /**
         * returns true if this range is immediately preceeding the given id
         */
        auto is_predecessor_of(id_type id) const noexcept -> bool;
        /**
         * returns true if this range is immediately succeeding the given id
         */
        auto is_successor_of(id_type id) const noexcept -> bool;

    private:
        underlying_type mFirstId;
        underlying_type mLastId;
    };      

    /**
     * \private
     */
    template <typename IdType>
    struct block_range_key_accessor final
    {
        using range_type = id_range<IdType>;
        using type = typename range_type::id_type;

        inline auto operator()(const range_type &self) -> type
        {
            return self.id();
        }
    };

    /**
     * manages id allocations by tracking unallocated id ranges
     */
    template <typename IdType>
    class block_manager
    {
    public:
        /**
         * the id type used in the public interface
         */
        using id_type = IdType;

    private:
        using range_type = id_range<id_type>;
        using allocator_type = std::pmr::polymorphic_allocator<range_type>;
        using allocator_traits = std::allocator_traits<allocator_type>;

        using block_set = boost::intrusive::avl_set<
            range_type, boost::intrusive::key_of_value<block_range_key_accessor<id_type>>>;

    public:
        block_manager();
        ~block_manager();

        /**
         * allocates the first available block 
         *
         * \returns errc::resource_exhausted if none are available
         */
        auto alloc_one() noexcept -> result<id_type>;
        /**
         * allocates many blocks and stores their id in the ids parameter
         *
         * \returns the number of successful allocations or errc::resource_exhausted
         */
        auto alloc_multiple(span<id_type> ids) noexcept -> result<std::size_t>;
        /**
         * allocates num contiguous blocks
         *
         * \returns the start id or errc::resource_exhausted
         */
        auto alloc_contiguous(const std::size_t num) noexcept -> result<id_type>;

        /**
         * tries to extend the contiguous block range represented by [begin, end] by num additional
         * blocks.
         *
         * \returns the new begin id or errc::resource_exhausted if the request couldn't be served
         */
        auto extend(const id_type begin, const id_type end, const std::uint64_t num) noexcept
            -> result<id_type>;

        /**
         * adds a block to the block pool
         *
         * \returns errc::not_enough_memory if a new node couldn't be allocated
         */
        auto dealloc_one(const id_type which) noexcept -> result<void>;
        /**
         * adds a contiguous block range [begin, begin + num) to the block pool
         */
        auto dealloc_contiguous(const id_type begin, const std::size_t num) noexcept
            -> result<void>;

        /**
         * serializes the state within the range [begin, begin + num) to the given bitset 
         */
        void write_to_bitset(bitset_overlay data, const IdType begin, const std::size_t num) const
            noexcept;
        /**
         * deserializes the state from the given bitset into the range [begin, begin + num)
         *
         * \returns errc::not_enough_memory if not enough memory could be allocated
         */
        auto parse_bitset(const const_bitset_overlay data, const IdType begin,
                          const std::size_t num) noexcept -> result<void>;

        /**
         * removes all blocks from the pool
         */
        void clear() noexcept;

    private:
        void dispose(typename block_set::const_iterator cit) noexcept;
        void dispose(typename block_set::const_iterator cbegin,
                     typename block_set::const_iterator cend) noexcept;

        block_set mFreeBlocks;
        std::unique_ptr<std::pmr::unsynchronized_pool_resource> mNodePool;
        allocator_type mAllocator;
    };

#pragma region id_range implementation

    template <typename IdType>
    inline id_range<IdType>::id_range(id_type first, id_type last) noexcept
        : mFirstId(static_cast<underlying_type>(first))
        , mLastId(static_cast<underlying_type>(last))
    {
    }

    template <typename IdType>
    inline auto id_range<IdType>::advance(id_type id, difference_type num) noexcept -> id_type
    {
        return static_cast<id_type>(static_cast<underlying_type>(id) + num);
    }

    template <typename IdType>
    inline auto id_range<IdType>::distance(id_type from, id_type to) noexcept -> difference_type
    {
        return static_cast<difference_type>(static_cast<underlying_type>(to) -
                                            static_cast<underlying_type>(from));
    }

    template <typename IdType>
    inline auto id_range<IdType>::id() const noexcept -> id_type
    {
        return last();
    }

    template <typename IdType>
    inline auto id_range<IdType>::first() const noexcept -> id_type
    {
        return static_cast<id_type>(mFirstId);
    }

    template <typename IdType>
    inline auto id_range<IdType>::last() const noexcept -> id_type
    {
        return static_cast<id_type>(mLastId);
    }

    template <typename IdType>
    inline auto id_range<IdType>::pop_front() noexcept -> id_type
    {
        return static_cast<id_type>(mFirstId++);
    }

    template <typename IdType>
    inline auto id_range<IdType>::pop_front(std::size_t num) noexcept -> id_type
    {
        return static_cast<id_type>(std::exchange(mFirstId, mFirstId + num));
    }

    template <typename IdType>
    inline auto id_range<IdType>::pop_front(span<id_type> ids) noexcept -> std::size_t
    {
        const auto num = std::min(ids.size(), size());

        // std::iota would have been the go to solution if id_type weren't an enum
        std::generate_n(ids.begin(), num, [this]() { return static_cast<id_type>(mFirstId++); });
        return num;
    }

    template <typename IdType>
    inline auto id_range<IdType>::pop_back(std::size_t num) noexcept -> id_type
    {
        mLastId -= num;
        return static_cast<id_type>(mLastId + 1);
    }

    template <typename IdType>
    inline void id_range<IdType>::prepend(std::size_t num) noexcept
    {
        mFirstId -= num;
    }

    template <typename IdType>
    inline void id_range<IdType>::append(std::size_t num) noexcept
    {
        mLastId += num;
    }

    template <typename IdType>
    inline auto id_range<IdType>::empty() noexcept -> bool
    {
        return mLastId < mFirstId;
    }

    template <typename IdType>
    inline auto id_range<IdType>::size() const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(mLastId - mFirstId + 1);
    }

    template <typename IdType>
    inline auto id_range<IdType>::is_predecessor_of(id_type id) const noexcept -> bool
    {
        return mLastId == static_cast<underlying_type>(id) - 1;
    }

    template <typename IdType>
    inline auto id_range<IdType>::is_successor_of(id_type id) const noexcept -> bool
    {
        return mFirstId == static_cast<underlying_type>(id) + 1;
    }

#pragma endregion

#pragma region block manager implementation

    template <typename IdType>
    inline block_manager<IdType>::block_manager()
        : mFreeBlocks()
        , mNodePool(new std::pmr::unsynchronized_pool_resource())
        , mAllocator(mNodePool.get())
    {
    }

    template <typename IdType>
    inline block_manager<IdType>::~block_manager()
    {
        clear();
    }

    template <typename IdType>
    inline auto block_manager<IdType>::alloc_one() noexcept -> result<id_type>
    {
        if (mFreeBlocks.empty())
        {
            return errc::resource_exhausted;
        }
        auto it = mFreeBlocks.begin();
        auto result = it->pop_front();

        if (it->empty())
        {
            dispose(it);
        }

        return result;
    }

    template <typename IdType>
    inline auto block_manager<IdType>::alloc_multiple(span<id_type> ids) noexcept
        -> result<std::size_t>
    {
        auto remaining = ids;

        const auto blocksBegin = mFreeBlocks.begin();
        auto blockIt = blocksBegin;

        for (const auto blocksEnd = mFreeBlocks.end(); remaining && blockIt != blocksEnd; ++blockIt)
        {
            const auto served = blockIt->pop_front(remaining);
            remaining = remaining.subspan(served);
        }
        if (const auto lastUsed = std::prev(blockIt); !lastUsed->empty())
        {
            blockIt = lastUsed;
        }
        dispose(blocksBegin, blockIt);

        return ids.size() - remaining.size();
    }

    template <typename IdType>
    inline auto block_manager<IdType>::alloc_contiguous(const std::size_t num) noexcept
        -> result<id_type>
    {
        auto end = mFreeBlocks.end();
        auto it = std::find_if(mFreeBlocks.begin(), end,
                               [num](range_type &r) { return r.size() >= num; });

        if (it == end)
        {
            return errc::resource_exhausted;
        }
        auto startId = it->pop_front(num);
        if (it->empty())
        {
            dispose(it);
        }
        return startId;
    }

    template <typename IdType>
    inline auto block_manager<IdType>::extend(const id_type begin, const id_type end,
                                              const std::uint64_t num) noexcept -> result<id_type>
    {
        if (mFreeBlocks.empty())
        {
            return errc::resource_exhausted;
        }

        auto succIt = mFreeBlocks.upper_bound(begin);
        const auto blocksBegin = mFreeBlocks.begin();
        const auto blocksEnd = mFreeBlocks.end();

        // valid & adjacent
        bool canUseSucc = succIt != blocksEnd && succIt->is_successor_of(end);
        if (canUseSucc && succIt->size() >= num)
        {
            succIt->pop_front(num);
            if (succIt->empty())
            {
                dispose(succIt);
            }
            return begin;
        }

        auto precIt = succIt;
        if (precIt != blocksBegin && (--precIt)->is_predecessor_of(begin))
        {
            const auto remaining = canUseSucc ? num - succIt->size() : num;
            if (precIt->size() >= remaining)
            {
                auto first = precIt->pop_back(remaining);
                if (precIt->empty())
                {
                    dispose(precIt);
                }
                if (canUseSucc)
                {
                    dispose(succIt);
                }

                return first;
            }
        }

        return errc::resource_exhausted;
    }

    template <typename IdType>
    inline auto block_manager<IdType>::dealloc_one(const id_type which) noexcept -> result<void>
    {
        return dealloc_contiguous(which, 1);
    }

    template <typename IdType>
    inline auto block_manager<IdType>::dealloc_contiguous(const id_type first,
                                                          const std::size_t num) noexcept
        -> result<void>
    {
        if (num == 0)
        {
            return success();
        }

        const auto last = range_type::advance(first, num - 1);
        const auto blocksBegin = mFreeBlocks.begin();
        const auto succIt = mFreeBlocks.upper_bound(first);
        const auto blocksEnd = mFreeBlocks.end();

        if (succIt != blocksEnd)
        {
            bool inserted = false;
            if (succIt->is_successor_of(last))
            {
                succIt->prepend(num);
                inserted = true;
            }
            if (succIt != blocksBegin)
            {
                if (auto precIt = std::prev(succIt); precIt->is_predecessor_of(first))
                {
                    if (inserted)
                    {
                        succIt->prepend(precIt->size());
                        dispose(precIt);
                    }
                    else
                    {
                        precIt->append(1);
                        inserted = true;
                    }
                }
            }
            if (inserted)
            {
                return success();
            }
        }
        try
        {
            auto node = allocator_traits::allocate(mAllocator, 1);
            allocator_traits::construct(mAllocator, node, first, last);
            mFreeBlocks.insert_before(succIt, *node);
            return success();
        }
        catch (const std::bad_alloc &)
        {
            return errc::not_enough_memory;
        }
    }

    template <typename IdType>
    inline void block_manager<IdType>::write_to_bitset(bitset_overlay data, const IdType begin,
                                                       const std::size_t num) const noexcept
    {
        if (num == 0)
        {
            return;
        }

        data.set_n(num);
        const auto last = range_type::advance(begin, num - 1);

        auto blockIt = mFreeBlocks.lower_bound(begin);
        if (blockIt == mFreeBlocks.end())
        {
            return;
        }

        auto blockEnd = mFreeBlocks.upper_bound(last);
        if (blockEnd != mFreeBlocks.end() && blockEnd->first() <= last)
        {
            std::advance(blockEnd, 1);
        }

        for (; blockIt != blockEnd; ++blockIt)
        {
            auto start = std::max<typename range_type::difference_type>(
                range_type::distance(begin, blockIt->first()), 0);
            auto end = std::min<typename range_type::difference_type>(
                range_type::distance(begin, blockIt->last()), num);

            for (; start <= end; ++start)
            {
                data.unset(start);
            }
        }
        return;
    }

    template <typename IdType>
    inline auto block_manager<IdType>::parse_bitset(const const_bitset_overlay data,
                                                    const IdType begin,
                                                    const std::size_t num) noexcept -> result<void>
    {
        typename range_type::difference_type start = -1;
        for (std::size_t i = 0; i < num; ++i)
        {
            if (data[i])
            {
                if (start >= 0)
                {
                    dealloc_contiguous(range_type::advance(begin, start), i - start);
                }
                start = -1;
            }
            else if (start < 0)
            {
                start = i;
            }
        }
        if (start >= 0)
        {
            dealloc_contiguous(range_type::advance(begin, start), num - start);
        }
    }

    template <typename IdType>
    void block_manager<IdType>::clear() noexcept
    {
        mFreeBlocks.clear_and_dispose([this](range_type *p) {
            allocator_traits::destroy(mAllocator, p);
            allocator_traits::deallocate(mAllocator, p, 1);
        });
    }

    template <typename IdType>
    inline void block_manager<IdType>::dispose(typename block_set::const_iterator cit) noexcept
    {
        mFreeBlocks.erase_and_dispose(cit, [this](range_type *p) {
            allocator_traits::destroy(mAllocator, p);
            allocator_traits::deallocate(mAllocator, p, 1);
        });
    }

    template <typename IdType>
    inline void block_manager<IdType>::dispose(typename block_set::const_iterator cbegin,
                                               typename block_set::const_iterator cend) noexcept
    {
        mFreeBlocks.erase_and_dispose(cbegin, cend, [this](range_type *p) {
            allocator_traits::destroy(mAllocator, p);
            allocator_traits::deallocate(mAllocator, p, 1);
        });
    }
#pragma endregion
} // namespace vefs::utils