#include "precompiled.hpp"
#include <vefs/detail/thread_pool_gen.hpp>

#include <atomic>
#include <exception>
#include <stdexcept>
#include <functional>

#include <boost/predef.h>


namespace vefs::detail
{
    void set_current_thread_name(const std::string &name);

    namespace
    {
        inline std::string make_anonymous_pool_name()
        {
            static std::atomic_int mAnonymousThreadPoolId{ 0 };

            std::string mThreadPoolName{};

            mThreadPoolName += "pool {";
            mThreadPoolName += std::to_string(mAnonymousThreadPoolId++);
            mThreadPoolName += "}";
            mThreadPoolName.shrink_to_fit();

            return mThreadPoolName;
        }
    }

    thread_pool_gen::thread_pool_gen(int minWorkers, int maxWorkers, std::string_view poolName)
        : mTaskQueue{}
        , mWorkerList{}
        , mThreadPoolName{ poolName.size() ? std::string{ poolName } : make_anonymous_pool_name() }
    {
        assert(minWorkers >= 0);
        assert(maxWorkers > 0 && maxWorkers >= minWorkers);

        mWorkerList.reserve(minWorkers);
        try
        {
            for (int i = 0; i < minWorkers; ++i)
            {
                mWorkerList.emplace_back(std::mem_fn(&thread_pool_gen::worker_main), this,
                    moodycamel::ConsumerToken(mTaskQueue), i);
            }
        }
        catch (...)
        {
            if (mWorkerList.size())
            {
                // we need to get rid of all already existing worker threads
                try
                {
                    for (auto i = 0; i < mWorkerList.size(); ++i)
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

    thread_pool_gen::~thread_pool_gen()
    {
        for (auto i = 0; i < mWorkerList.size(); ++i)
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
            std::string threadName = mThreadPoolName + "; thread {" + std::to_string(id) + "}";
            set_current_thread_name(threadName);
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
}

#if defined BOOST_OS_WINDOWS_AVAILABLE

#include <vefs/utils/windows-proper.h>

namespace vefs::detail
{
#pragma pack(push, 8)

    void set_current_thread_name(const std::string &name)
    {
        // adapted from https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code

        constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

        struct THREADNAME_INFO
        {
            DWORD dwType;     // Must be 0x1000.
            LPCSTR szName;    // Pointer to name (in user addr space).
            DWORD dwThreadID; // Thread ID (-1=caller thread).
            DWORD dwFlags;    // Reserved for future use, must be zero.
        } info {
            0x1000,
            name.c_str(),
            static_cast<DWORD>(-1),
            0
        };

        static_assert(std::is_pod_v<decltype(info)>);

#pragma warning(push)
#pragma warning(disable: 6320 6322)
        __try
        {
            RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
#pragma warning(pop)
    }

#pragma pack(pop)
}

#elif defined BOOST_OS_LINUX_AVAILABLE

#include <pthread.h>

namespace vefs::detail
{
    void set_current_thread_name(const std::string &name)
    {
        const auto id = pthread_self();
        pthread_setname_np(id, name.c_str());
    }
}

#elif defined BOOST_OS_MACOS_AVAILABLE

#include <pthread.h>

namespace vefs::detail
{
    void set_current_thread_name(const std::string &name)
    {
        pthread_setname_np(name.c_str());
    }
}

#else

namespace vefs::detail
{
    void set_current_thread_name(const std::string &name)
    {
    }
}

#endif



