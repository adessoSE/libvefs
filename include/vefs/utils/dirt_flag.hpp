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

    inline auto get() -> state;
    inline auto is_dirty() -> bool;

private:
    std::atomic<bool> mState;
};

inline dirt_flag::dirt_flag()
    : dirt_flag{state::clean}
{
}

inline dirt_flag::dirt_flag(state initialState)
    : mState{initialState == state::dirty}
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

inline auto dirt_flag::get() -> dirt_flag::state
{
    return mState.load(std::memory_order_acquire) ? state::dirty : state::clean;
}

inline auto dirt_flag::is_dirty() -> bool
{
    return mState.load(std::memory_order_acquire);
}
} // namespace vefs::utils
