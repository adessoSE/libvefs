#pragma once

#include <memory>
#include <utility>
#include <shared_mutex>

#include <vefs/archive.hpp>

#include "archive_file.hpp"
#include "archive_internal_file.hpp"

namespace vefs
{
    class archive::free_block_list_file
        : public archive::internal_file
    {
    public:
        free_block_list_file(archive &owner, detail::basic_archive_file_meta &meta);
        free_block_list_file(archive &owner, detail::basic_archive_file_meta &meta, create_tag);

        template <typename... Args>
        static inline std::shared_ptr<archive::free_block_list_file> create(Args &&... args);
    };

    template<typename ...Args>
    inline std::shared_ptr<archive::free_block_list_file> archive::free_block_list_file::create(Args &&... args)
    {
        return std::make_shared<archive::free_block_list_file>(std::forward<Args>(args)...);
    }
}
