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
        friend class internal_file;
        using free_block_map = std::map<detail::sector_id, std::uint64_t>;

    public:
        free_block_list_file(archive &owner);

        static auto open(archive &owner)
            -> result<std::shared_ptr<free_block_list_file>>;
        static auto create_new(archive &owner)
            -> result<std::shared_ptr<free_block_list_file>>;

        using internal_file::dispose;

        auto alloc_sector()
            -> result<detail::sector_id>;
        auto alloc_sectors(span<detail::sector_id> dest)
            -> result<void>;

        void dealloc_sector(detail::sector_id sector);
        void dealloc_sectors(span<detail::sector_id> sectors);

        auto sync()
            -> result<void>;

    private:
        auto parse_content()
            -> result<void>;

        auto grow_owner_impl(std::uint64_t num)
            -> result<free_block_map::iterator>;
        void dealloc_sectors_impl(span<detail::sector_id> sectors);

        virtual void on_sector_write_suggestion(sector_handle sector) override;
        virtual void on_root_sector_synced(detail::basic_archive_file_meta &rootMeta) override;
        virtual void on_sector_synced(detail::sector_id physId, ro_blob<16> mac) override;

        std::mutex mFreeBlockSync;
        free_block_map mFreeBlockMap;
    };

    inline auto archive::free_block_list_file::alloc_sector()
        -> result<detail::sector_id>
    {
        detail::sector_id xmem;
        BOOST_OUTCOME_TRY(alloc_sectors({ &xmem, 1 }));
        return xmem;
    }
}
