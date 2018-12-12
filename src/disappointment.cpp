#include "precompiled.hpp"
#include <vefs/disappointment.hpp>

namespace vefs
{
    BOOST_NOINLINE error_info::error_info() noexcept
        : mDetails{ }
    {
    }
    BOOST_NOINLINE error_info::~error_info()
    {
    }

    auto error::success_domain::name() const noexcept
        -> std::string_view
    {
        using namespace std::string_view_literals;
        return "success-domain"sv;
    }
    auto error::success_domain::message(const error &, const error_code code) const noexcept
        -> std::string_view
    {
        using namespace std::string_view_literals;
        return code == 0
            ? "success"sv
            : "invalid-success-code"sv;
    }
    const error::success_domain error::success_domain::sInstance;
    auto error::success_domain::instance() noexcept
        -> const error_domain &
    {
        return sInstance;
    }

    auto error::diagnostic_information(error_message_format format) const noexcept
        -> std::string
    {
        using namespace std::string_view_literals;

        auto domain = mDomain->name();
        auto errorDesc = mDomain->message(*this, code());


        if (format != error_message_format::simple && has_info())
        {
            error_info::diagnostics_buffer buffer;
            fmt::format_to(buffer, "{} => {}", domain, errorDesc);

            info()->diagnostic_information(buffer, "\n\t");

            return to_string(buffer);
        }
        else
        {
            return fmt::format(FMT_STRING("{} => {}"), domain, errorDesc);
        }
    }

    const char * error_exception::what() const noexcept
    {
        if (mErrDesc.size() == 0)
        {
            try
            {
                mErrDesc = mErr.diagnostic_information(error_message_format::with_diagnostics);
            }
            catch (const std::bad_alloc &)
            {
                return "<error_exception|failed to allocate the diagnostic information string>";
            }
            catch (...)
            {
                return "<error_exception|failed to retrieve the diagnostic information from the error code>";
            }
        }
        return mErrDesc.c_str();
    }
}

namespace vefs
{
    class generic_domain_type final
        : public error_domain
    {
        auto name() const noexcept
            ->std::string_view override;
        auto message(const error &, const error_code code) const noexcept
            ->std::string_view override;
    };

    auto generic_domain_type::name() const noexcept
        -> std::string_view
    {
        using namespace std::string_view_literals;

        return "generic"sv;
    }

    auto generic_domain_type::message(const error &, const error_code value) const noexcept
        -> std::string_view
    {
        using namespace std::string_view_literals;


        switch (const errc code{ value }; code)
        {
        case errc::bad:
            return "unexpected failure in third party code"sv;

        case errc::invalid_argument:
            return "a given function argument did not meet its requirements"sv;

        case errc::key_already_exists:
            return "the given key already existed in the collection"sv;

        case errc::not_enough_memory:
            return "could not allocate the required memory"sv;

        case errc::not_supported:
            return "the requested feature is not supported"sv;

        case errc::result_out_of_range:
            return "function not defined for the given arguments (result would be out of range)"sv;

        case errc::user_object_copy_failed:
            return "the function call failed due to an exception thrown by a user object copy ctor"sv;

        default:
            return "unknown generic error code"sv;
        }
    }

    namespace
    {
        constexpr generic_domain_type generic_domain_v;
    }

    auto generic_domain() noexcept
        -> const error_domain &
    {
        return generic_domain_v;
    }

    class archive_domain_type final
        : public error_domain
    {
        auto name() const noexcept
            -> std::string_view override;
        auto message(const error &, const error_code code) const noexcept
            -> std::string_view override;
    };

    auto archive_domain_type::name() const noexcept
        -> std::string_view
    {
        using namespace std::string_view_literals;

        return "vefs-archive"sv;
    }

    auto archive_domain_type::message(const error &, const error_code value) const noexcept
        -> std::string_view
    {
        using namespace std::string_view_literals;


        switch (const archive_errc code{ value }; code)
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

    auto archive_domain() noexcept
        -> const error_domain &
    {
        return archive_domain_v;
    }
}
