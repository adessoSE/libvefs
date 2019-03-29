#include "precompiled.hpp"
#include <vefs/detail/thread_pool_win32.hpp>

#include <vefs/exceptions.hpp>
#include <vefs/utils/misc.hpp>


#include <vefs/utils/windows-proper.h>

namespace vefs::detail
{
    void __stdcall thread_pool_win32_default::tpw32_callback(void *, void *pWork)
    {
        std::unique_ptr<task_t> work{ static_cast<task_t *>(pWork) };
        if (work)
        {
            xdo(*work);
        }
    }

    void thread_pool_win32_default::execute(std::unique_ptr<task_t> task)
    {
        assert(task);
        if (TrySubmitThreadpoolCallback(reinterpret_cast<PTP_SIMPLE_CALLBACK>(&tpw32_callback),
            task.get(), nullptr))
        {
            task.release();
        }
        else
        {
            std::error_code ec{ static_cast<int>(GetLastError()), std::system_category() };
            BOOST_THROW_EXCEPTION(std::system_error(ec));
        }
    }
}
