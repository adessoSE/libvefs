#include <vefs/platform/thread_pool.hpp>

#include <cassert>

#include <vefs/utils/misc.hpp>

#include "thread_pool_gen.hpp"
#include "thread_pool_win32.hpp"

namespace vefs::detail
{

auto thread_pool::shared() -> thread_pool &
{
#if defined BOOST_OS_WINDOWS_AVAILABLE
    static thread_pool_win32_default pool;
#else
    static thread_pool_gen pool{std::thread::hardware_concurrency() * 2,
                                std::thread::hardware_concurrency() * 2,
                                "vefs-process-shared"};
#endif
    return pool;
}

void thread_pool::xdo(task_t &work) noexcept
{
    try
    {
        work();
    }
    catch (...)
    {
    }
}

#if __cpp_lib_atomic_wait < 201'907L
pooled_work_tracker::pooled_work_tracker(thread_pool *pool)
    : mPool{pool}
    , mWorkCtr{0}
    , mSync{}
    , mOnDecr{}
{
}
#else
pooled_work_tracker::pooled_work_tracker(thread_pool *pool)
    : mPool{pool}
    , mWorkCtr{0}
{
}
#endif

void pooled_work_tracker::wait()
{
#if __cpp_lib_atomic_wait < 201'907L
    std::unique_lock lock{mSync};
    mOnDecr.wait(lock, [this]() {
        return mWorkCtr.load(std::memory_order_acquire) == 0;
    });
#else
    auto currentValue = mWorkCtr.load(std::memory_order::acquire);
    while (currentValue > 0)
    {
        mWorkCtr.wait(currentValue, std::memory_order::acquire);
        currentValue = mWorkCtr.load(std::memory_order::acquire);
    }
#endif
}

void pooled_work_tracker::execute(std::unique_ptr<task_t> task)
{
    assert(task);

    mWorkCtr.fetch_add(1, std::memory_order::release);
    VEFS_ERROR_EXIT
    {
        if (0 == mWorkCtr.fetch_sub(1, std::memory_order::acq_rel))
        {
#if __cpp_lib_atomic_wait < 201'907L
            mOnDecr.notify_all();
#else
            mWorkCtr.notify_all();
#endif
        }
    };

    mPool->execute([this, xtask = std::move(*task)]() mutable {
        VEFS_SCOPE_EXIT
        {
            if (0 == mWorkCtr.fetch_sub(1, std::memory_order::acq_rel))
            {
#if __cpp_lib_atomic_wait < 201'907L
                mOnDecr.notify_all();
#else
                mWorkCtr.notify_all();
#endif
            }
        };

        xtask();
    });
}

} // namespace vefs::detail
