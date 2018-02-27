#pragma once

#include <cstddef>

#include <list>
#include <array>
#include <vector>
#include <mutex>
#include <system_error>

#include <vefs/exceptions.hpp>
#include <vefs/filesystem.hpp>
#include <vefs/detail/thread_pool.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/secure_ops.hpp>
#include <vefs/utils/unordered_map_mt.hpp>

namespace vefs::tests
{
    enum class memvefs_code
    {
        no_write_mode,
        no_read_mode,
        out_of_range,
        file_not_found,
        out_of_memory,
    };
    const std::error_category & memvefs_category();
    inline std::error_code make_error_code(memvefs_code code)
    {
        return { static_cast<int>(code), memvefs_category() };
    }
}
namespace std
{
    template <>
    struct is_error_code_enum<vefs::tests::memvefs_code>
        : std::true_type
    {
    };
}

namespace vefs::tests
{
    struct memory_filesystem;

    struct memory_file
        : public file
    {
        struct memory_holder
        {
            static constexpr std::size_t chunk_size = 1 << 20;

            using chunk = std::array<std::byte, chunk_size>;
            using chunk_list = std::list<chunk>;
            using lock_t = std::lock_guard<std::mutex>;

            memory_holder()
            {
                mChunks.emplace_back();
            }

            void resize(std::size_t size)
            {
                lock_t sync{ mGrowMutex };
                // this allows to simulate sparse disk space conditions
                if (size > mMaxSize)
                {
                    BOOST_THROW_EXCEPTION(std::bad_alloc{});
                }
                if (mCurrentSize != size)
                {
                    const auto numBlocks = utils::div_ceil(size, chunk_size);

                    // if resize throws the size will not be changed (strong exception guarantee)
                    mChunks.resize(numBlocks);
                    if (mCurrentSize > size)
                    {
                        // burn anything that has been cut off, but would still continue to
                        // exist on the last chunk
                        const auto fraction = size % chunk_size;
                        if (fraction)
                        {
                            utils::secure_memzero(blob{ mChunks.back() }.slice(fraction));
                        }
                    }
                    mCurrentSize = size;
                }
            }
            std::size_t size() const
            {
                lock_t sync{ mGrowMutex };
                return mCurrentSize;
            }
            template <typename Fn>
            void access(std::size_t offset, std::size_t size, Fn &&fn)
            {
                if (!size)
                {
                    fn(blob{});
                    return;
                }

                using iter_t = chunk_list::iterator;
                iter_t it;
                {
                    lock_t sync{ mGrowMutex };
                    it = mChunks.begin();
                }
                const auto beginIdx = offset / chunk_size;
                // endIdx is the idx past the end
                // can be == beginIdx if and only if (offset % chunk_size == 0 && size == 0)
                const auto endIdx = utils::div_ceil(offset + size, chunk_size);
                std::advance(it, beginIdx);

                if (endIdx - beginIdx <= 1)
                {
                    // the area to be processed is contained within the same chunk
                    fn(blob{ *it }.slice(offset % chunk_size, size));
                }
                else
                {
                    iter_t end = it;
                    std::advance(end, endIdx - beginIdx - 1);

                    fn(blob{ *it++ }.slice(offset % chunk_size));
                    for (; it != end; ++it)
                    {
                        fn(blob{ *it });
                    }
                    fn(blob{ *it }.slice(0, (offset + size) % chunk_size));
                }
            }

            mutable std::mutex mGrowMutex;
            chunk_list mChunks = { };
            std::size_t mCurrentSize = 0;
            std::size_t mMaxSize = std::numeric_limits<std::size_t>::max();
        };

        memory_file(std::shared_ptr<memory_filesystem> owner, std::shared_ptr<memory_holder> memory, file_open_mode_bitset mode)
            : mOwner(owner)
            , mMemory(memory)
            , mOpenMode(mode)
        {
        }

        virtual void read(blob buffer, std::uint64_t readFilePos, std::error_code & ec) override;
        virtual std::future<void> read_async(blob buffer, std::uint64_t readFilePos, async_callback_fn callback) override;

        virtual void write(blob_view data, std::uint64_t writeFilePos, std::error_code & ec) override;
        virtual std::future<void> write_async(blob_view data, std::uint64_t writeFilePos, async_callback_fn callback) override;

        virtual void sync(std::error_code & ec) override;
        virtual std::future<void> sync_async(async_callback_fn callback) override;

        virtual std::uint64_t size(std::error_code & ec) override;
        virtual void resize(std::uint64_t newSize, std::error_code & ec) override;
        virtual std::future<void> resize_async(std::uint64_t newSize, async_callback_fn callback) override;

        std::shared_ptr<memory_filesystem> mOwner;
        std::shared_ptr<memory_holder> mMemory;
        file_open_mode_bitset mOpenMode;
    };

    struct memory_filesystem
        : public filesystem
    {
        using relaxed_string_map
            = utils::unordered_string_map_mt<std::shared_ptr<memory_file::memory_holder>>;

        memory_filesystem();

        static std::shared_ptr<memory_filesystem> create()
        {
            auto fs = std::shared_ptr<memory_filesystem>{ new memory_filesystem };
            fs->self = fs;

            return fs;
        }

        using filesystem::open;
        virtual file::ptr open(std::string_view filePath, file_open_mode_bitset mode, std::error_code & ec) override;
        virtual void remove(std::string_view filePath) override;

        vefs::detail::thread_pool &opsPool;
        relaxed_string_map files;

    private:
        std::weak_ptr<memory_filesystem> self;
    };
}
