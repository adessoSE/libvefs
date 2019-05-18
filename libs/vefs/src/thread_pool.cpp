#include "precompiled.hpp"
#include <vefs/detail/thread_pool.hpp>

#include <cassert>

#include <vefs/utils/misc.hpp>
#include <vefs/detail/thread_pool_gen.hpp>
#include <vefs/detail/thread_pool_win32.hpp>

namespace vefs::detail
{
    thread_pool & thread_pool::shared()
    {
#if defined BOOST_OS_WINDOWS_AVAILABLE
        static thread_pool_win32_default pool;
#else
        static thread_pool_gen pool{
            std::thread::hardware_concurrency() * 2,
            std::thread::hardware_concurrency() * 2,
            "vefs-process-shared"
        };
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

    pooled_work_tracker::pooled_work_tracker(thread_pool * pool)
        : mPool{ pool }
        , mSync{}
        , mWorkCtr{ 0 }
        , mOnDecr{}
    {
    }

    void pooled_work_tracker::wait()
    {
        std::unique_lock lock{ mSync };
        mOnDecr.wait(lock, [this]() { return mWorkCtr.load(std::memory_order_acquire) == 0; });
    }

    void pooled_work_tracker::execute(std::unique_ptr<task_t> task)
    {
        assert(task);

        mWorkCtr.fetch_add(1, std::memory_order_release);
        VEFS_ERROR_EXIT{ mWorkCtr.fetch_sub(1, std::memory_order_release); };

        mPool->execute([this, xtask = std::move(*task)]() mutable
        {
            VEFS_SCOPE_EXIT{
                mWorkCtr.fetch_sub(1, std::memory_order_release);
                mOnDecr.notify_all();
            };

            xtask();
        });
    }
}
