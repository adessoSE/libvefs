#include "precompiled.hpp"
#include <vefs/disappointment.hpp>

namespace vefs
{
    std::string_view error_info::success_domain_t::name() const noexcept
    {
        using namespace std::string_view_literals;
        return "<success-domain>"sv;
    }
    std::string_view error_info::success_domain_t::message(intptr_t value) const noexcept
    {
        using namespace std::string_view_literals;
        return value == 0
            ? "<success>"sv
            : "<invalid-success-code>"sv;
    }

    auto error_info::diagnostic_information(bool verbose) const
        -> std::string
    {
        return "";
    }

    const char * error_exception::what() const noexcept
    {
        if (mErrDesc.size() == 0)
        {
            try
            {
                mErrDesc = mErr.diagnostic_information();
            }
            catch (const std::bad_alloc &)
            {
                return "<failed to allocate the diagnostic information string>";
            }
        }
        return mErrDesc.c_str();
    }
}

namespace vefs
{
    class archive_domain_type final
        : public error_domain
    {
        virtual std::string_view name() const noexcept override;
        virtual std::string_view message(intptr_t value) const noexcept override;
    };

    std::string_view archive_domain_type::name() const noexcept
    {
        using namespace std::string_view_literals;

        return "vefs-archive"sv;
    }

    std::string_view archive_domain_type::message(intptr_t value) const noexcept
    {
        using namespace std::string_view_literals;

        auto code = archive_errc{ value };
        switch (code)
        {
        case vefs::archive_errc::invalid_prefix:
            return "the magic number at the beginning of the archive didn't match"sv;

        case vefs::archive_errc::oversized_static_header:
            return "the static archive header would be greater than the master sector"sv;

        case vefs::archive_errc::no_archive_header:
            return "no valid archive header could be read"sv;

        case vefs::archive_errc::identical_header_version:
            return "both archive headers were valid and contained the same version switch"sv;

        case vefs::archive_errc::tag_mismatch:
            return "decryption failed because the message tag didn't match"sv;

        case vefs::archive_errc::invalid_proto:
            return "the protobuf message decoding failed"sv;

        case vefs::archive_errc::incompatible_proto:
            return "the protobuf message contained invalid values"sv;

        case vefs::archive_errc::sector_reference_out_of_range:
            return "a sector reference pointed to a sector which currently isn't allocated"sv;

        case vefs::archive_errc::corrupt_index_entry:
            return "an entry from the archive index is corrupted and could not be read"sv;

        case vefs::archive_errc::free_sector_index_invalid_size:
            return "the free sector index has an invalid size"sv;

        default:
            return "unknown vefs archive error code"sv;
        }
    }

    namespace
    {
        constexpr archive_domain_type archive_domain_v;
    }

    const error_domain & archive_domain() noexcept
    {
        return archive_domain_v;
    }
}
