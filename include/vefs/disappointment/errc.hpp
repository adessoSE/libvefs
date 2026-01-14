#pragma once

#include <dplx/cncr/data_defined_status_domain.hpp>
#include <dplx/predef/compiler.h>

#include <status-code/status_code.hpp>

#include <vefs/disappointment/fwd.hpp>

#if defined(DPLX_COMP_GNUC_AVAILABLE)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif

namespace vefs
{

namespace system_error = SYSTEM_ERROR2_NAMESPACE;

enum class archive_errc : error_code
{
    success = 0,
    invalid_prefix,
    oversized_static_header,
    no_archive_header,
    identical_header_version,
    tag_mismatch,
    sector_reference_out_of_range,
    corrupt_index_entry,
    no_such_vfile,
    wrong_user_prk,
    vfilesystem_invalid_size,
    archive_file_already_existed,
    archive_file_did_not_exist,
    bad,
    resource_exhausted,
    still_in_use,
    not_loaded,
    no_more_data,
};

} // namespace vefs

template <>
struct dplx::cncr::status_enum_definition<vefs::archive_errc>
    : status_enum_definition_defaults<vefs::archive_errc>
{
    static constexpr char domain_id[]
            = "{9F10BF2E-4F20-459E-9976-4D975CBB3349}";
    static constexpr char domain_name[] = "vefs-domain";

    static constexpr value_descriptor values[] = {
            // clang-format off
            {                      code::success,                   generic_errc::success, "the operation completed successfully"                                                                            },
            {               code::invalid_prefix,               generic_errc::bad_message, "the magic number at the beginning of the archive didn't match"                                                   },
            {      code::oversized_static_header,               generic_errc::bad_message, "the static archive header would be greater than the master sector"                                               },
            {            code::no_archive_header,               generic_errc::bad_message, "no valid archive header could be read"                                                                           },
            {     code::identical_header_version,               generic_errc::bad_message, "both archive headers were valid and contained the same version switch"                                           },
            {                 code::tag_mismatch,               generic_errc::bad_message, "decryption failed because the message tag didn't match"                                                          },
            {code::sector_reference_out_of_range,               generic_errc::bad_message, "a sector reference pointed to a sector which currently isn't allocated"                                          },
            {          code::corrupt_index_entry,               generic_errc::bad_message, "an entry from the archive index is corrupted and could not be read"                                              },
            {                code::no_such_vfile, generic_errc::no_such_file_or_directory, "no file has been found under the given name"                                                                     },
            {               code::wrong_user_prk,          generic_errc::invalid_argument, "the given archive key is not valid for this archive or the archive head has been corrupted"                      },
            {     code::vfilesystem_invalid_size,               generic_errc::bad_message, "the vfilesystem storage extent is not a multiple of the sector_payload_size"                                     },
            { code::archive_file_already_existed,               generic_errc::file_exists, "the given file already contained data which would be overwritten, but creation::only_if_not_exist was specified" },
            {   code::archive_file_did_not_exist, generic_errc::no_such_file_or_directory, "the given file contained no data, but creation::open_existing"                                                   },
            {                          code::bad,          generic_errc::invalid_argument, "an API precondition has been violated"                                                                           },
            {           code::resource_exhausted,                   generic_errc::unknown, "the archive has run out of free sectors"                                                                         },
            {                 code::still_in_use,                   generic_errc::unknown, "the archive is still in use by other handles"                                                                    },
            {                   code::not_loaded,                   generic_errc::unknown, "the sector has not been loaded"                                                                                  },
            {                 code::no_more_data,                   generic_errc::unknown, "there is no more data to read"                                                                                   },
            // clang-format on
    };
};

namespace vefs
{

using archive_code = dplx::cncr::data_defined_status_code<archive_errc>;

} // namespace vefs

#if defined(DPLX_COMP_GNUC_AVAILABLE)
#pragma GCC diagnostic pop
#endif
