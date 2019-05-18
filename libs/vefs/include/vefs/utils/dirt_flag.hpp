#pragma once

#include <atomic>

namespace vefs::utils
{
    class dirt_flag
    {
    public:
        enum class state
        {
            clean,
            dirty,
        };

        inline dirt_flag();
        inline explicit dirt_flag(state initialState);

        inline void set(state nextState);
        inline void mark();
        inline void unmark();

        inline state get();
        inline bool is_dirty();

    private:
        std::atomic<bool> mState;
    };

    inline dirt_flag::dirt_flag()
        : dirt_flag{ state::clean }
    {
    }

    inline dirt_flag::dirt_flag(state initalState)
        : mState{ initalState == state::dirty }
    {
    }

    inline void dirt_flag::set(state nextState)
    {
        mState.store(nextState == state::dirty, std::memory_order_release);
    }

    inline void dirt_flag::mark()
    {
        set(state::dirty);
    }

    inline void dirt_flag::unmark()
    {
        set(state::clean);
    }

    inline dirt_flag::state dirt_flag::get()
    {
        return mState.load(std::memory_order_acquire)
            ? state::dirty : state::clean;
    }

    inline bool dirt_flag::is_dirty()
    {
        return mState.load(std::memory_order_acquire);
    }
}
