#include "precompiled.hpp"
#include <vefs/detail/thread_pool.hpp>

#include <exception>
#include <stdexcept>
#include <functional>

namespace vefs
{

thread_pool::thread_pool(unsigned int numWorkers)
    : mTaskQueue()
    , mWorkerList()
{
    try
    {
        for (unsigned int i = 0; i < numWorkers; ++i)
        {
            mWorkerList.emplace_back(std::mem_fn(&thread_pool::worker_main), this,
                moodycamel::ConsumerToken(mTaskQueue));
        }
    }
    catch (const std::exception &)
    {
        if (mWorkerList.size())
        {
            // we need to get rid of all already existing worker threads
            try
            {
                for (auto i = 0; i < mWorkerList.size(); ++i)
                {
                    mTaskQueue.enqueue(task_t{});
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

thread_pool::~thread_pool()
{
    for (auto i = 0; i < mWorkerList.size(); ++i)
    {
        mTaskQueue.enqueue(task_t{});
    }

    for (auto &worker : mWorkerList)
    {
        worker.join();
    }
}

void thread_pool::worker_main(moodycamel::ConsumerToken workerToken)
{
    task_t task;
    for (;;)
    {
        mTaskQueue.wait_dequeue(workerToken, task);

        if (!task)
        {
            break;
        }

        task();
    }
}

}
