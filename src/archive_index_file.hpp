#pragma once

#include <memory>
#include <utility>
#include <optional>
#include <shared_mutex>
#include <limits>

#include <vefs/archive.hpp>
#include <vefs/utils/dirt_flag.hpp>

#include "archive_file.hpp"
#include "archive_internal_file.hpp"
#include "block_manager.hpp"

namespace vefs
{
    class archive::index_file final
        : private file_events
        , public archive::internal_file
    {
        using index_t = utils::unordered_string_map_mt<detail::file_id>;
        using handle_map_t = utils::unordered_map_mt<detail::file_id, file_lookup_ptr>;

        enum class init_tag {};

        static constexpr auto block_size = 64u;
        static constexpr auto alloc_map_size = 64u;
        static constexpr auto blocks_per_sector
            = (detail::raw_archive::sector_payload_size - alloc_map_size) / block_size;

        static constexpr auto sector_padding
            = detail::raw_archive::sector_payload_size
                - alloc_map_size - (blocks_per_sector * block_size);

        static_assert(alloc_map_size * std::numeric_limits<std::underlying_type_t<std::byte>>::digits
                        > blocks_per_sector);

        static inline detail::tree_position treepos_of(int metaBlockPos)
        {
            return detail::tree_position{ metaBlockPos / blocks_per_sector };
        }

    public:
        index_file(archive &owner, detail::basic_archive_file_meta &meta);
        index_file(archive &owner, detail::basic_archive_file_meta &meta, create_tag);

        template <typename... Args>
        static inline std::shared_ptr<archive::index_file> create(Args &&... args);

        auto open(const std::string_view filePath, const file_open_mode_bitset mode)
            -> file_handle;
        void erase(std::string_view filePath);

        auto query(const std::string_view filePath) -> std::optional<file_query_result>;

        auto sync(bool full) -> bool;
        auto sync_open_files() -> bool;
        void notify_meta_update(file_lookup_ptr lookup, file *ws);

    private:
        virtual void on_sector_write_suggestion(sector_handle sector) override;
        virtual void on_root_sector_synced(detail::basic_archive_file_meta &rootMeta) override;
        virtual void on_sector_synced(detail::sector_id physId, blob_view mac) override;

        void parse_content();

        void dealloc_blocks(int first, int num);

        void write_blocks(int indexBlockPos, blob_view data, bool updateAllocMap);
        auto write_blocks_impl(int mIndexBlockPos, blob_view data, bool updateAllocMap)
            -> std::optional<std::tuple<int, blob_view>>;
        void write_block_header(sector_handle handle);

        index_t mIndex;
        std::mutex mIOSync;
        handle_map_t mFileHandles;
        utils::block_manager<std::uint64_t> mFreeBlocks;
        utils::dirt_flag mDirtFlag;
    };

    template<typename ...Args>
    inline std::shared_ptr<archive::index_file> archive::index_file::create(Args &&... args)
    {
        return std::make_shared<archive::index_file>(std::forward<Args>(args)...);
    }
}
