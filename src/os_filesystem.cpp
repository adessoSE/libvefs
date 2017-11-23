#include "precompiled.hpp"
#include "os_filesystem.hpp"

namespace vefs
{
    filesystem::ptr os_filesystem()
    {
        return detail::os_filesystem::create();
    }
}

namespace vefs::detail
{
    os_file::os_file(std::shared_ptr<os_filesystem> owner,
        os_handle fileHandle)
        : mOwner(std::move(owner))
        , mFile(fileHandle)
    {
    }

    std::future<void> os_file::read_async(blob buffer, std::uint64_t readFilePos,
        file::async_callback_fn callback)
    {
        return mOwner->ops_pool()
            .exec([this, buffer, readFilePos](async_callback_fn callback)
        {
            std::error_code ec;
            read(buffer, readFilePos, ec);
            callback(std::move(ec));
        }, std::move(callback));
    }

    std::future<void> os_file::write_async(blob_view data, std::uint64_t writeFilePos,
        file::async_callback_fn callback)
    {
        return mOwner->ops_pool()
            .exec([this, data, writeFilePos](async_callback_fn callback)
        {
            std::error_code ec;
            write(data, writeFilePos, ec);
            callback(std::move(ec));
        }, std::move(callback));
    }

    std::future<void> os_file::sync_async(file::async_callback_fn callback)
    {
        return mOwner->ops_pool()
            .exec([this](async_callback_fn callback)
        {
            std::error_code ec;
            sync(ec);
            callback(std::move(ec));
        }, std::move(callback));
    }

    std::future<void> os_file::resize_async(std::uint64_t newSize,
        file::async_callback_fn callback)
    {
        return mOwner->ops_pool()
            .exec([this, newSize](async_callback_fn callback)
        {
            std::error_code ec;
            resize(newSize, ec);
            callback(std::move(ec));
        }, std::move(callback));
    }
}
