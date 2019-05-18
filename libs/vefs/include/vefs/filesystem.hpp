#pragma once

#include <cstddef>
#include <cstdint>

#include <memory>
#include <future>
#include <string_view>
#include <system_error>

#include <vefs/blob.hpp>
#include <vefs/utils/enum_bitset.hpp>

namespace vefs
{
    enum class file_open_mode
    {
        read = 0b0000,
        write = 0b0001,
        readwrite = read | write,
        truncate = 0b0010,
        create = 0b0100,
    };

    std::true_type allow_enum_bitset(file_open_mode &&);
    using file_open_mode_bitset = enum_bitset<file_open_mode>;


    class file
    {
    public:
        using ptr = std::shared_ptr<file>;
        using async_callback_fn = std::function<void(std::error_code)>;

        virtual ~file() = default;

        virtual void read(rw_dynblob buffer, std::uint64_t readFilePos);
        virtual void read(rw_dynblob buffer, std::uint64_t readFilePos, std::error_code &ec) = 0;
        virtual std::future<void> read_async(rw_dynblob buffer, std::uint64_t readFilePos,
            async_callback_fn callback) = 0;

        virtual void write(ro_dynblob data, std::uint64_t writeFilePos);
        virtual void write(ro_dynblob data, std::uint64_t writeFilePos, std::error_code &ec) = 0;
        virtual std::future<void> write_async(ro_dynblob data, std::uint64_t writeFilePos,
            async_callback_fn callback) = 0;

        virtual void sync();
        virtual void sync(std::error_code &ec) = 0;
        virtual std::future<void> sync_async(async_callback_fn callback) = 0;

        virtual std::uint64_t size();
        virtual std::uint64_t size(std::error_code &ec) = 0;

        virtual void resize(std::uint64_t newSize);
        virtual void resize(std::uint64_t newSize, std::error_code &ec) = 0;
        virtual std::future<void> resize_async(std::uint64_t newSize,
            async_callback_fn callback) = 0;
    };

    class filesystem
    {
    public:
        using ptr = std::shared_ptr<filesystem>;

        virtual ~filesystem() = default;

        virtual file::ptr open(std::string_view filePath, file_open_mode_bitset mode);
        virtual file::ptr open(std::string_view filePath, file_open_mode_bitset mode,
            std::error_code &ec) = 0;

        virtual void remove(std::string_view filePath) = 0;
    };

    filesystem::ptr os_filesystem();
}
