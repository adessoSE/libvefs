#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <limits>

#include <boost/integer.hpp>

#include <vefs/utils/misc.hpp>

namespace vefs::detail
{
    /**
     * Implements a dynamic clock data structure for up to ClockSize elements. Requires O(ClockSize)
     * space.
     *
     * The internal implementation uses the smallest available integer type for the stored indices.
     */
    template <std::uintmax_t ClockSize>
    class cache_clock
    {
        using size_type = typename boost::uint_value_t<ClockSize>::fast;
        using index_type = typename boost::uint_value_t<ClockSize>::fast;
        using entry_index = typename boost::uint_value_t<ClockSize * 2>::least;

    public:
        static constexpr entry_index one = 1;
        static constexpr entry_index tombstone_bit =
            one << (std::numeric_limits<entry_index>::digits - 1);
        static_assert(tombstone_bit > ClockSize);

        static constexpr auto max_size() -> std::size_t
        {
            return ClockSize;
        }

        /**
         * Removes the front element from the clock queue and returns it.
         */
        auto pop_front() noexcept -> std::size_t;
        /**
         * Inserts the given index at the tail of the clock queue.
         */
        void push_back(std::size_t value) noexcept;
        /**
         * Clears the queue of all elements and resets the size target.
         */
        void clear() noexcept;

        /**
         * Removes the given index from the clock. O(ClockSize)
         */
        bool purge(std::size_t value) noexcept;

        auto size() const noexcept -> std::size_t;
        auto size_target() const noexcept -> std::size_t;
        void size_target(std::size_t value) noexcept;

    private:
        auto next() noexcept -> index_type;
        static constexpr auto entry_init(std::size_t) noexcept -> entry_index
        {
            // everyone is dead...
            return tombstone_bit;
        }
        using entries = std::array<entry_index, max_size()>;

        index_type mHand{};
        size_type mSize{};
        size_type mSizeTarget{};
        entries mEntries{utils::sequence_init<entries, max_size()>(entry_init)};
    };

    template <std::uintmax_t ClockSize>
    inline auto cache_clock<ClockSize>::next() noexcept -> index_type
    {
        if (++mHand >= max_size())
        {
            mHand = 0;
        }
        return mHand;
    }

    template <std::uintmax_t ClockSize>
    inline auto cache_clock<ClockSize>::pop_front() noexcept -> std::size_t
    {
        assert(size() > 0);
        --mSize;
        // skip any fields which are dead
        while ((mEntries[mHand] & tombstone_bit) != 0)
        {
            next();
        }
        mEntries[mHand] = tombstone_bit;
        return static_cast<std::size_t>(mHand);
    }

    template <std::uintmax_t ClockSize>
    inline void cache_clock<ClockSize>::push_back(std::size_t value) noexcept
    {
        assert(value < max_size());
        ++mSize;
        // skip any fields which are alive
        while ((mEntries[mHand] & tombstone_bit) == 0)
        {
            next();
        }
        mEntries[mHand] = static_cast<entry_index>(value);
        next();
    }

    template <std::uintmax_t ClockSize>
    inline void cache_clock<ClockSize>::clear() noexcept
    {
        mHand = 0;
        mSize = 0;
        mSizeTarget = 0;
        for (auto &entry : mEntries)
        {
            entry = entry_init(0);
        }
    }

    template <std::uintmax_t ClockSize>
    inline bool cache_clock<ClockSize>::purge(std::size_t value) noexcept
    {
        const auto it =
            std::find(mEntries.begin(), mEntries.end(), static_cast<entry_index>(value));
        if (it == mEntries.end())
        {
            return false;
        }
        *it = tombstone_bit;
        --mSize;
        return true;
    }

    template <std::uintmax_t ClockSize>
    inline auto cache_clock<ClockSize>::size() const noexcept -> std::size_t
    {
        return mSize;
    }
    template <std::uintmax_t ClockSize>
    inline auto cache_clock<ClockSize>::size_target() const noexcept -> std::size_t
    {
        return mSizeTarget;
    }
    template <std::uintmax_t ClockSize>
    inline void cache_clock<ClockSize>::size_target(std::size_t value) noexcept
    {
        mSizeTarget = static_cast<size_type>(value);
    }
} // namespace vefs::detail
