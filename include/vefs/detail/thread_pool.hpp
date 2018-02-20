#pragma once

#include <string>
#include <thread>
#include <future>
#include <functional>
#include <type_traits>
#include <string_view>

#include <vefs/ext/concurrentqueue/blockingconcurrentqueue.h>

namespace vefs::detail
{
    class thread_pool
    {
        using task_t = std::function<void()>;

    public:
        explicit thread_pool(unsigned int numWorkers = std::thread::hardware_concurrency(),
            std::string_view poolName = {});
        thread_pool(const thread_pool &) = delete;
        thread_pool(thread_pool &&) = delete;
        ~thread_pool();

        thread_pool & operator=(const thread_pool &) = delete;
        thread_pool & operator=(thread_pool &&) = delete;

        template <typename T, typename... Args >
        auto exec(T &&task, Args&&... args);

        template <typename Fn>
        void exec(Fn &&task);

        template <typename Fn>
        auto exec_with_completion(Fn &&task);

    private:
        void worker_main(moodycamel::ConsumerToken workerToken, int id);

        moodycamel::BlockingConcurrentQueue<task_t> mTaskQueue;
        std::vector<std::thread> mWorkerList;
        const std::string mThreadPoolName;
    };

    template <typename Fn>
    inline void thread_pool::exec(Fn &&task)
    {
        if constexpr (noexcept(task()))
        {
            // this part won't be compiled if Fn is a std::function
            // because the std::function::operator() is noexcept(false)
            // therefore you cannot sneakily invoke either the copy or move ctor
            mTaskQueue.enqueue(task_t{ std::forward<Fn>(task) });
        }
        else
        {
            mTaskQueue.enqueue([btask = std::forward<Fn>(task)]() mutable noexcept
            {
                try
                {
                    btask();
                }
                catch (...)
                {
                    // maybe trigger the debugger or something
                }
            });
        }
    }

    template <typename Fn>
    inline auto thread_pool::exec_with_completion(Fn &&task)
    {
        using return_type = decltype(task());
        auto taskPackage = std::make_shared<std::packaged_task<return_type()>>(
            std::forward<Fn>(task)
        );
        auto taskResult = taskPackage->get_future();

        this->exec([task = std::move(taskPackage)]() mutable noexcept { (*task)(); });

        return taskResult;
    }
}
