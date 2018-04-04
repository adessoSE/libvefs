#include "precompiled.hpp"
#include "archive_index_file.hpp"

namespace vefs
{
    archive::index_file::index_file(archive &owner, detail::basic_archive_file_meta &meta)
        : file_events{}
        , archive::internal_file{ owner, meta, *this }
    {
    }
    archive::index_file::index_file(archive &owner, detail::basic_archive_file_meta &meta,
        create_tag)
        : file_events{}
        , archive::internal_file{ owner, meta, *this, archive::create }
    {
    }

    void archive::index_file::on_sector_write_suggestion(sector_handle sector)
    {
        on_dirty_sector(std::move(sector));
    }

    void archive::index_file::on_root_sector_synced(detail::basic_archive_file_meta &rootMeta)
    {
    }

    void archive::index_file::on_sector_synced(detail::sector_id physId, blob_view mac)
    {
    }
}
