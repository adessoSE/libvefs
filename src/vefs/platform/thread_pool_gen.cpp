#include "thread_pool_gen.hpp"

#include <atomic>
#include <exception>
#include <functional>
#include <stdexcept>

#include <boost/predef.h>

#include <vefs/platform/platform.hpp>

namespace vefs::detail
{
namespace
{
inline auto make_anonymous_pool_name() -> std::string
{
    static std::atomic_int mAnonymousThreadPoolId{0};

    std::string mThreadPoolName{};

    mThreadPoolName += "pool {";
    mThreadPoolName += std::to_string(mAnonymousThreadPoolId++);
    mThreadPoolName += "}";
    mThreadPoolName.shrink_to_fit();

    return mThreadPoolName;
}
} // namespace

thread_pool_gen::thread_pool_gen(unsigned minWorkers,
                                 [[maybe_unused]] unsigned maxWorkers,
                                 std::string_view poolName)
    : mTaskQueue{}
    , mWorkerList{}
    , mThreadPoolName{!poolName.empty() ? std::string{poolName}
                                        : make_anonymous_pool_name()}
{
    assert(maxWorkers >= minWorkers);

    mWorkerList.reserve(minWorkers);
    try
    {
        for (unsigned i = 0; i < minWorkers; ++i)
        {
            mWorkerList.emplace_back(std::mem_fn(&thread_pool_gen::worker_main),
                                     this,
                                     moodycamel::ConsumerToken(mTaskQueue), i);
        }
    }
    catch (...)
    {
        if (!mWorkerList.empty())
        {
            // we need to get rid of all already existing worker threads
            try
            {
                for (std::size_t i = 0; i < mWorkerList.size(); ++i)
                {
                    mTaskQueue.enqueue(work_item_t{});
                }

                for (auto &worker : mWorkerList)
                {
                    if (worker.joinable())
                    {
                        worker.join();
                    }
                }
            }
            catch (...)
            {
                // this may seem a little bit harsh, but it is the only sane
                // action considering that we potentially have active threads
                // which will access the task queue which in turn will cease to
                // exist after rethrowing
                std::terminate();
            }
        }
        throw;
    }
}

thread_pool_gen::~thread_pool_gen() noexcept
{
    for (std::size_t i = 0, end = mWorkerList.size(); i < end; ++i)
    {
        mTaskQueue.enqueue(work_item_t{});
    }

    for (auto &worker : mWorkerList)
    {
        worker.join();
    }
}

void thread_pool_gen::worker_main(moodycamel::ConsumerToken workerToken, int id)
{
    {
        std::string threadName
                = mThreadPoolName + "; thread {" + std::to_string(id) + "}";
        utils::set_current_thread_name(threadName);
    }

    for (;;)
    {
        work_item_t task;
        mTaskQueue.wait_dequeue(workerToken, task);

        if (!task)
        {
            break;
        }

        xdo(*task);
    }
}

void thread_pool_gen::execute(std::unique_ptr<task_t> task)
{
    mTaskQueue.enqueue(std::move(task));
}
} // namespace vefs::detail
