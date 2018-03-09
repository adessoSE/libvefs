#include "precompiled.hpp"
#include <vefs/exceptions.hpp>

#include <boost/predef.h>

#if defined BOOST_OS_WINDOWS_AVAILABLE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined BOOST_OS_LINUX_AVAILABLE || defined BOOST_OS_MACOS_AVAILABLE
#include <cerrno>
#endif

namespace vefs
{
    errinfo_code make_system_errinfo_code()
    {
#if defined BOOST_OS_WINDOWS_AVAILABLE
        std::error_code ec{ static_cast<int>(GetLastError()), std::system_category() };
#elif defined BOOST_OS_LINUX_AVAILABLE || defined BOOST_OS_MACOS_AVAILABLE
        std::error_code ec{ errno, std::system_category() };
#endif
        return errinfo_code{ ec };
    }

    namespace
    {
        class vefs_error_category
            : public std::error_category
        {
        public:
            // Inherited via error_category
            virtual const char * name() const noexcept override
            {
                return "vefs";
            }

            virtual std::string message(int errval) const override
            {
                /*
                switch (vefs_error_code{ errval })
                {
                default:

                }
                */
                return std::string{ "unknown mvefs error code: #" } +std::to_string(errval);
            }

        };

        class archive_error_category
            : public std::error_category
        {
            // Inherited via error_category
            virtual const char * name() const noexcept override
            {
                return "vefs-archive";
            }

            virtual std::string message(int errval) const override
            {
                auto code = archive_error_code{ errval };
                switch (code)
                {
                case vefs::archive_error_code::invalid_prefix:
                    return "the magic number at the beginning of the archive didn't match";

                case vefs::archive_error_code::oversized_static_header:
                    return "the static archive header would be greater than the master sector";

                case vefs::archive_error_code::no_archive_header:
                    return "no valid archive header could be read";

                case vefs::archive_error_code::identical_header_version:
                    return "both archive headers were valid and contained the same version switch";

                case vefs::archive_error_code::tag_mismatch:
                    return "decryption failed because the message tag didn't match";

                case vefs::archive_error_code::invalid_proto:
                    return "the protobuf message decoding failed";

                case vefs::archive_error_code::incompatible_proto:
                    return "the protobuf message contained invalid values";

                case vefs::archive_error_code::sector_reference_out_of_range:
                    return "a sector reference pointed to a sector which currently isn't allocated";

                case vefs::archive_error_code::corrupt_index_entry:
                    return "an entry from the archive index is corrupted and could not be read";

                case vefs::archive_error_code::free_sector_index_invalid_size:
                    return "the free sector index has an invalid size";

                default:
                    return std::string{ "unknown vefs archive error code: #" } +std::to_string(errval);
                }
            }
        };

        const vefs_error_category gVefsErrorCategory;
        const archive_error_category gArchiveErrorCategory;
    }

    const std::error_category & vefs_category()
    {
        return gVefsErrorCategory;
    }

    const std::error_category & archive_category()
    {
        return gArchiveErrorCategory;
    }
}
