#pragma once

#include <functional>
#include <thread>
#include <type_traits>

#include <boost/predef.h>

#include <vefs/platform/thread_pool.hpp>

#if defined BOOST_OS_WINDOWS_AVAILABLE

namespace vefs::detail
{
class thread_pool_win32_default final : public thread_pool
{
public:
    thread_pool_win32_default() = default;
    ~thread_pool_win32_default() = default;

private:
    static void __stdcall tpw32_callback(void *, void *pWork);

    // Inherited via thread_pool
    virtual void execute(std::unique_ptr<task_t> task) override;
};
} // namespace vefs::detail

#endif
