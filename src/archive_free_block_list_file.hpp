#pragma once

#include <mutex>
#include <memory>
#include <utility>

#include <vefs/archive.hpp>

#include "archive_file.hpp"
#include "archive_internal_file.hpp"

namespace vefs
{
    class archive::free_block_list_file final
        : private file_events
        , private archive::internal_file
    {
        using free_block_map = std::map<detail::sector_id, std::uint64_t>;

    public:
        free_block_list_file(archive &owner, detail::basic_archive_file_meta &meta);
        free_block_list_file(archive &owner, detail::basic_archive_file_meta &meta, create_tag);
        using internal_file::dispose;

        template <typename... Args>
        static inline std::shared_ptr<archive::free_block_list_file> create(Args &&... args);

        inline auto alloc_sector();
        std::vector<detail::sector_id> alloc_sectors(unsigned int num);

        void dealloc_sectors(std::vector<detail::sector_id> sectors);

        void sync();

    private:
        free_block_map::iterator grow_owner_impl(unsigned int num);
        void dealloc_sectors_impl(std::vector<detail::sector_id> sectors);

        virtual void on_sector_write_suggestion(sector_handle sector) override;
        virtual void on_root_sector_synced(detail::basic_archive_file_meta &rootMeta) override;
        virtual void on_sector_synced(detail::sector_id physId, blob_view mac) override;

        std::mutex mFreeBlockSync;
        free_block_map mFreeBlockMap;
    };

    template<typename ...Args>
    inline std::shared_ptr<archive::free_block_list_file> archive::free_block_list_file::create(Args &&... args)
    {
        return std::make_shared<archive::free_block_list_file>(std::forward<Args>(args)...);
    }

    inline auto archive::free_block_list_file::alloc_sector()
    {
        return alloc_sectors(1).front();
    }
}