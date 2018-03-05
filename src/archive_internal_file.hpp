#pragma once

#include <memory>
#include <utility>
#include <shared_mutex>

#include <vefs/archive.hpp>

#include "archive_file.hpp"

namespace vefs
{
    class archive::internal_file
        : public std::enable_shared_from_this<archive::internal_file>
        , public archive::file
    {
    public:
        internal_file(archive &owner, detail::basic_archive_file_meta &meta);
        internal_file(archive &owner, detail::basic_archive_file_meta &meta, create_tag);

        void dispose();

    private:
        void on_dirty_sector(block_pool_t::handle sector);

        std::shared_mutex mLifetimeSync;
        bool mDisposed;
    };
}
