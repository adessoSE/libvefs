#pragma once

#include <functional>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

#include <dplx/predef/compiler.h>
#if defined(DPLX_COMP_MSVC_AVAILABLE)
#pragma warning(push, 3)
#pragma warning(disable : 4702) // unreachable code
#endif
#include <concurrentqueue/blockingconcurrentqueue.h>
#if defined(DPLX_COMP_MSVC_AVAILABLE)
#pragma warning(pop)
#endif
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
    virtual ~thread_pool_gen() noexcept;

private:
    void worker_main(moodycamel::ConsumerToken workerToken, int id);

    // Inherited via thread_pool
    void execute(std::unique_ptr<task_t> task) override;

    moodycamel::BlockingConcurrentQueue<work_item_t> mTaskQueue;
    std::vector<std::thread> mWorkerList;
    const std::string mThreadPoolName;
};
} // namespace vefs::detail
