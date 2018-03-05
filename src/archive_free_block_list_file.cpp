#include "precompiled.hpp"
#include "archive_free_block_list_file.hpp"

namespace vefs
{
    archive::free_block_list_file::free_block_list_file(archive &owner,
        detail::basic_archive_file_meta &meta)
        : archive::internal_file{ owner, meta }
    {
    }
    archive::free_block_list_file::free_block_list_file(archive &owner,
        detail::basic_archive_file_meta &meta, create_tag)
        : archive::internal_file{ owner, meta, archive::create }
    {
    }
}
