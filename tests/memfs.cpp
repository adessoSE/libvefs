#include "memfs.hpp"

#include <stdexcept>

#include <boost/predef.h>
#include "boost-unit-test.hpp"

#include <vefs/detail/thread_pool.hpp>

namespace vefs::tests
{
    namespace
    {
        class memvefs_category_impl
            : public std::error_category
        {
        public:
#if !defined BOOST_COMP_MSVC_AVAILABLE
            constexpr
#endif
                memvefs_category_impl() = default;


            // Inherited via error_category
            virtual const char * name() const noexcept override
            {
                return "memvefs";
            }

            virtual std::string message(int errval) const override
            {
                switch (memvefs_code{ errval })
                {
                case memvefs_code::no_write_mode: return "requested a file write on a file not opened for writing";
                case memvefs_code::no_read_mode: return "requested a file read on a file not opend for reading";
                case memvefs_code::out_of_range: return "tried to read past the end of the file";
                case memvefs_code::file_not_found: return "the requested file wasn't found";
                case memvefs_code::out_of_memory: return "could not grow the file, because not enough memory was available";
                default:
                    return std::string{ "unknown memvefs error code: #" } + std::to_string(errval);
                }
            }

        };
#if defined BOOST_COMP_MSVC_AVAILABLE
        const
#else
        constexpr
#endif
        memvefs_category_impl gMemvefsCategory;
    }

    const std::error_category & memvefs_category()
    {
        return gMemvefsCategory;
    }

    void memory_file::read(blob buffer, std::uint64_t readFilePos, std::error_code & ec)
    {
        if (!buffer)
        {
            BOOST_THROW_EXCEPTION(logic_error{});
        }
        if (!(mOpenMode % file_open_mode::read))
        {
            ec = memvefs_code::no_read_mode;
            return;
        }
        if (readFilePos + buffer.size() > mMemory->size())
        {
            ec = memvefs_code::out_of_range;
            return;
        }
        mMemory->access(readFilePos, buffer.size(), [&buffer](blob_view dataChunk)
        {
            dataChunk.copy_to(buffer);
            buffer.remove_prefix(dataChunk.size());
        });
    }

    void memory_file::write(blob_view data, std::uint64_t writeFilePos, std::error_code & ec)
    {
        if (!data)
        {
            BOOST_THROW_EXCEPTION(logic_error{});
        }
        if (!(mOpenMode % file_open_mode::write))
        {
            ec = memvefs_code::no_write_mode;
            return;
        }
        if (const auto reqFileSize = writeFilePos + data.size(); reqFileSize > mMemory->size())
        {
            try
            {
                mMemory->resize(reqFileSize);
            }
            catch (const std::bad_alloc &)
            {
                ec = memvefs_code::out_of_memory;
                return;
            }
        }
        mMemory->access(writeFilePos, data.size(), [&data](blob chunk)
        {
            data.copy_to(chunk);
            data.remove_prefix(chunk.size());
        });
    }

    void memory_file::sync(std::error_code & ec)
    {
        if (!(mOpenMode % file_open_mode::write))
        {
            ec = memvefs_code::no_write_mode;
            return;
        }
    }

    std::uint64_t memory_file::size(std::error_code &)
    {
        return mMemory->size();
    }
    void memory_file::resize(std::uint64_t newSize, std::error_code & ec)
    {
        if (!(mOpenMode % file_open_mode::write))
        {
            ec = memvefs_code::no_write_mode;
            return;
        }
        try
        {
            mMemory->resize(static_cast<std::size_t>(newSize));
        }
        catch (const std::bad_alloc &)
        {
            ec = memvefs_code::out_of_memory;
        }
    }


    file::ptr memory_filesystem::open(std::string_view filePath, file_open_mode_bitset mode, std::error_code & ec)
    {
        try
        {
            std::shared_ptr<memory_file::memory_holder> fileHandle;
            if (!files.find(filePath, fileHandle))
            {
                if (mode % file_open_mode::create)
                {
                    fileHandle = std::make_shared<memory_file::memory_holder>();
                    files.uprase_fn(filePath, [&fileHandle](std::shared_ptr<memory_file::memory_holder> &handle)
                    {
                        fileHandle = handle;
                        return false;
                    }, fileHandle);
                }
                else
                {
                    ec = memvefs_code::file_not_found;
                }
            }
            if (fileHandle)
            {
                if (mode % file_open_mode::truncate)
                {
                    if (mode % file_open_mode::write)
                    {
                        fileHandle->resize(0);
                    }
                    else
                    {
                        ec = memvefs_code::no_write_mode;
                        fileHandle.reset();
                    }
                }
                return std::static_pointer_cast<file>(
                    std::make_shared<memory_file>(shared_from_this(), fileHandle, mode)
                );
            }
        }
        catch (const std::bad_alloc &)
        {
            files.erase(filePath);
            ec = memvefs_code::out_of_memory;
        }
        return file::ptr{};
    }

    void memory_filesystem::remove(std::string_view filePath)
    {
        if (!files.erase(filePath))
        {
            //TODO: set error code
            BOOST_THROW_EXCEPTION(io_error{}
                << errinfo_code{ memvefs_code::file_not_found });
        }
    }


    std::future<void> memory_file::read_async(blob buffer, std::uint64_t readFilePos,
                                                file::async_callback_fn callback)
    {
        auto task = [this, buffer, readFilePos, cb = std::move(callback)]()
        {
            std::error_code ec;
            read(buffer, readFilePos, ec);
            cb(std::move(ec));
        };
        return vefs::detail::thread_pool::shared().twoway_execute(std::move(task));
    }

    std::future<void> memory_file::write_async(blob_view data, std::uint64_t writeFilePos,
                                                file::async_callback_fn callback)
    {
        auto task = [this, data, writeFilePos, cb = std::move(callback)]()
        {
            std::error_code ec;
            write(data, writeFilePos, ec);
            cb(std::move(ec));
        };
        return vefs::detail::thread_pool::shared().twoway_execute(std::move(task));
    }

    std::future<void> memory_file::sync_async(file::async_callback_fn callback)
    {
        auto task = [this, cb = std::move(callback)]()
        {
            std::error_code ec;
            sync(ec);
            cb(std::move(ec));
        };
        return vefs::detail::thread_pool::shared().twoway_execute(std::move(task));
    }

    std::future<void> memory_file::resize_async(std::uint64_t newSize,
                                                file::async_callback_fn callback)
    {
        auto task = [this, newSize, cb = std::move(callback)]()
        {
            std::error_code ec;
            resize(newSize, ec);
            cb(std::move(ec));
        };
        return vefs::detail::thread_pool::shared().twoway_execute(std::move(task));
    }
}
