#pragma once

#include <memory>
#include <utility>
#include <shared_mutex>

#include <vefs/archive.hpp>

#include "archive_file.hpp"
#include "archive_internal_file.hpp"

namespace vefs
{
    class archive::index_file
        : public archive::internal_file
    {
    public:
        index_file(archive &owner, detail::basic_archive_file_meta &meta);
        index_file(archive &owner, detail::basic_archive_file_meta &meta, create_tag);

        template <typename... Args>
        static inline std::shared_ptr<archive::index_file> create(Args &&... args);
    };

    template<typename ...Args>
    inline std::shared_ptr<archive::index_file> archive::index_file::create(Args &&... args)
    {
        return std::make_shared<archive::index_file>(std::forward<Args>(args)...);
    }
}
