#pragma once

#include <atomic>
#include <string>
#include <future>
#include <functional>
#include <type_traits>
#include <condition_variable>

namespace vefs::detail
{
    class thread_pool
    {
    public:
        using task_t = std::function<void()>;

        thread_pool(const thread_pool &) = delete;
        thread_pool(thread_pool &&) = delete;

        thread_pool & operator=(const thread_pool &) = delete;
        thread_pool & operator=(thread_pool &&) = delete;

        static thread_pool & shared();

        template <typename F>
        void execute(F &&task);

        template <typename F>
        auto twoway_execute(F &&twoway_task);

    protected:
        thread_pool() noexcept = default;
        ~thread_pool() = default;

        static void xdo(task_t &work) noexcept;

    private:
        virtual void execute(std::unique_ptr<task_t> task) = 0;
    };

    template <typename F>
    inline void thread_pool::execute(F &&task)
    {
        if constexpr (std::is_convertible_v<decltype(task), task_t>)
        {
            execute(std::make_unique<task_t>(std::forward<F>(task)));
        }
        else
        {
            execute(std::make_unique<task_t>([btask = std::forward<F>(task)]() mutable
            {
                std::invoke(btask);
            }));
        }
    }

    template<typename F>
    inline auto thread_pool::twoway_execute(F &&twoway_task)
    {
        using return_type = decltype(twoway_task());
        auto taskPackage = std::make_shared<std::packaged_task<return_type()>>(
            std::forward<F>(twoway_task)
        );
        auto taskResult = taskPackage->get_future();

        execute(std::make_unique<task_t>(
            [task = std::move(taskPackage)]() mutable noexcept { (*task)(); }
        ));

        return taskResult;
    }

    class pooled_work_tracker
        : public thread_pool
    {
    public:
        pooled_work_tracker() = delete;
        pooled_work_tracker(thread_pool *pool);
        ~pooled_work_tracker() = default;

        void wait();

    private:
        // Inherited via thread_pool
        virtual void execute(std::unique_ptr<task_t> task) override;

        thread_pool *const mPool;
        std::mutex mSync;
        std::atomic<int> mWorkCtr;
        std::condition_variable mOnDecr;
    };
}
