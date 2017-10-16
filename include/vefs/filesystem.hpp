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
        truncate = 0b0010,
        create = 0b0100,
    };

    std::true_type allow_enum_bitset(file_open_mode &&);
    using file_open_mode_bitset = enum_bitset<file_open_mode>;

    template <typename F>
    auto error_code_scope(F &&func)
    {
        std::error_code ec;
        if constexpr (std::is_same_v<decltype(func(ec)), void>)
        {
            func(ec);
            if (ec)
            {
                throw std::system_error(ec);
            }
        }
        else
        {
            auto result = func(ec);
            if (ec)
            {
                throw std::system_error(ec);
            }
            return result;
        }
    }


    class file
    {
    public:
        using ptr = std::shared_ptr<file>;
        using async_callback_fn = std::function<void(std::error_code)>;

        virtual ~file() = default;

        virtual void read(blob buffer, std::uint64_t readFilePos)
        {
            error_code_scope([&](auto &ec) { read(buffer, readFilePos, ec); });
        }
        virtual void read(blob buffer, std::uint64_t readFilePos, std::error_code &ec) = 0;
        virtual std::future<void> read_async(blob buffer, std::uint64_t readFilePos,
            async_callback_fn callback) = 0;

        virtual void write(blob_view data, std::uint64_t writeFilePos)
        {
            error_code_scope([&](auto &ec) { write(data, writeFilePos, ec); });
        }
        virtual void write(blob_view data, std::uint64_t writeFilePos, std::error_code &ec) = 0;
        virtual std::future<void> write_async(blob_view data, std::uint64_t writeFilePos,
            async_callback_fn callback) = 0;

        virtual void sync()
        {
            error_code_scope([this](auto &ec) { sync(ec); });
        }
        virtual void sync(std::error_code &ec) = 0;
        virtual std::future<void> sync_async(async_callback_fn callback) = 0;

        virtual std::uint64_t size()
        {
            return error_code_scope([this](auto &ec) { return size(ec); });
        }
        virtual std::uint64_t size(std::error_code &ec) = 0;

        virtual void resize(std::uint64_t newSize)
        {
            error_code_scope([&](auto &ec) { resize(newSize, ec); });
        }
        virtual void resize(std::uint64_t newSize, std::error_code &ec) = 0;
        virtual std::future<void> resize_async(std::uint64_t newSize,
            async_callback_fn callback) = 0;
    };

    class filesystem
    {
    public:
        using ptr = std::shared_ptr<filesystem>;

        virtual ~filesystem() = default;

        virtual file::ptr open(std::string_view filePath, file_open_mode_bitset mode)
        {
            return error_code_scope([&](auto &ec) { return open(filePath, mode, ec); });
        }
        virtual file::ptr open(std::string_view filePath, file_open_mode_bitset mode,
            std::error_code &ec) = 0;

        virtual void remove(std::string_view filePath) = 0;
    };

    filesystem::ptr os_filesystem();
}
