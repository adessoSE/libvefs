#pragma once

#include <thread>
#include <future>
#include <functional>
#include <type_traits>

#include <vefs/ext/concurrentqueue/blockingconcurrentqueue.h>

namespace vefs::detail
{
    class thread_pool
    {
        using task_t = std::function<void()>;

    public:
        explicit thread_pool(unsigned int numWorkers = std::thread::hardware_concurrency());
        thread_pool(const thread_pool &) = delete;
        thread_pool(thread_pool &&) = delete;
        ~thread_pool();

        thread_pool & operator=(const thread_pool &) = delete;
        thread_pool & operator=(thread_pool &&) = delete;

        template <typename T, typename... Args >
        auto exec(T &&task, Args&&... args);

    private:
        void worker_main(moodycamel::ConsumerToken workerToken);

        moodycamel::BlockingConcurrentQueue<task_t> mTaskQueue;
        std::vector<std::thread> mWorkerList;
    };

    template <typename T, typename... Args>
    auto thread_pool::exec(T&& task, Args&&... args)
    {
        using return_type = decltype(task(std::forward<Args>(args)...));
        auto taskPackage = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(task, std::forward<Args>(args)...)
        );
        auto taskResult = taskPackage->get_future();

        mTaskQueue.enqueue([taskPackage]() { (*taskPackage)(); });

        return taskResult;
    }
}
