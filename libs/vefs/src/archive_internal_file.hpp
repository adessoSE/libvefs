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
        void dispose();

    protected:
        internal_file(archive &owner, detail::basic_archive_file_meta &meta, file_events &hooks);
        template <typename T>
        static auto open(archive &owner)
            -> result<std::shared_ptr<T>>;
        template <typename T>
        static auto create_new(archive &owner)
            -> result<std::shared_ptr<T>>;

        void on_dirty_sector(block_pool_t::handle sector);

    private:
        std::shared_mutex mLifetimeSync;
        bool mDisposed;
    };

    template<typename T>
    inline auto archive::internal_file::open(archive & owner)
        -> result<std::shared_ptr<T>>
    {
        auto self = std::make_shared<T>(owner);
        OUTCOME_TRY(self->parse_content());

        return std::move(self);
    }
    template<typename T>
    inline auto archive::internal_file::create_new(archive & owner)
        -> result<std::shared_ptr<T>>
    {
        auto self = std::make_shared<T>(owner);
        OUTCOME_TRY(self->create_self());

        return std::move(self);
    }
}
