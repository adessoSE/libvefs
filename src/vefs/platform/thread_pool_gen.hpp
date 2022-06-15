#pragma once

#include <functional>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

#include <concurrentqueue/blockingconcurrentqueue.h>
#include <vefs/platform/thread_pool.hpp>

namespace vefs::detail
{

class thread_pool_gen : public thread_pool
{
    using work_item_t = std::unique_ptr<task_t>;

public:
    explicit thread_pool_gen(unsigned minWorkers
                             = std::thread::hardware_concurrency(),
                             unsigned maxWorkers
                             = std::thread::hardware_concurrency(),
                             std::string_view poolName = {});
    ~thread_pool_gen();

private:
    void worker_main(moodycamel::ConsumerToken workerToken, int id);

    // Inherited via thread_pool
    virtual void execute(std::unique_ptr<task_t> task) override;

    moodycamel::BlockingConcurrentQueue<work_item_t> mTaskQueue;
    std::vector<std::thread> mWorkerList;
    const std::string mThreadPoolName;
};
} // namespace vefs::detail
