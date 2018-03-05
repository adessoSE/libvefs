#include "precompiled.hpp"
#include "archive_index_file.hpp"

namespace vefs
{
    archive::index_file::index_file(archive &owner, detail::basic_archive_file_meta &meta)
        : archive::internal_file{ owner, meta }
    {
    }
    archive::index_file::index_file(archive &owner, detail::basic_archive_file_meta &meta,
        create_tag)
        : archive::internal_file{ owner, meta, archive::create }
    {
    }
}
