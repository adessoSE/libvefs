#include "precompiled.hpp"
#include <vefs/disappointment.hpp>

#include <ios>
#include <mutex>
#include <future>
#include <unordered_map>

#include <boost/predef.h>
#include <boost/config.hpp>

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

        decltype(auto) hDomain = domain();
        auto domain = hDomain.name();
        auto errorDesc = hDomain.message(*this, code());


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
            -> std::string_view override;
        auto message(const error &, const error_code code) const noexcept
            -> std::string_view override;

    public:
        constexpr generic_domain_type() noexcept = default;
    };

    auto generic_domain_type::name() const noexcept
        -> std::string_view
    {
        using namespace std::string_view_literals;

        return "generic-domain"sv;
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
        constexpr generic_domain_type generic_domain_v{};
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

    public:
        constexpr archive_domain_type() noexcept = default;
    };

    auto archive_domain_type::name() const noexcept
        -> std::string_view
    {
        using namespace std::string_view_literals;

        return "vefs-archive-domain"sv;
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
        constexpr archive_domain_type archive_domain_v{};
    }

    auto archive_domain() noexcept
        -> const error_domain &
    {
        return archive_domain_v;
    }
}

namespace vefs::adl::disappointment
{
    namespace
    {
        class std_adapter_domain final
            : public error_domain
        {
        public:
            constexpr std_adapter_domain(const std::error_category *impl) noexcept;

        private:
            auto name() const noexcept
                ->std::string_view override;
            auto message(const error &, const error_code code) const noexcept
                ->std::string_view override;

            const std::error_category * const mImpl;
        };

        constexpr std_adapter_domain::std_adapter_domain(const std::error_category *impl) noexcept
            : mImpl{ impl }
        {
        }

        auto std_adapter_domain::name() const noexcept
            -> std::string_view
        {
            return mImpl->name();
        }

        auto std_adapter_domain::message(const error &, const error_code code) const noexcept
            -> std::string_view
        {
            return mImpl->message(static_cast<int>(code));
        }


        const std_adapter_domain generic_cat_adapter{ &std::generic_category() };
        const std_adapter_domain system_cat_adapter{ &std::system_category() };
        const std_adapter_domain iostream_cat_adapter{ &std::iostream_category() };
        const std_adapter_domain future_cat_adapter{ &std::future_category() };

        std::mutex nonstandard_category_domain_map_sync{};
        std::unordered_map<const std::error_category *, std::unique_ptr<std_adapter_domain>>
            nonstandard_category_domain_map{ 8 };

        auto adapt_domain(const std::error_category &cat) noexcept
            -> const error_domain &
        {
            if (cat == std::generic_category())
            {
                return generic_cat_adapter;
            }
            if (cat == std::system_category())
            {
                return system_cat_adapter;
            }
            if (cat == std::future_category())
            {
                return future_cat_adapter;
            }
            if (cat == std::iostream_category())
            {
                return iostream_cat_adapter;
            }

            std::lock_guard lock{ nonstandard_category_domain_map_sync };
            if (auto it = nonstandard_category_domain_map.find(&cat);
                it != nonstandard_category_domain_map.end())
            {
                return *it->second;
            }

            return *nonstandard_category_domain_map.emplace(
                &cat, std::make_unique<std_adapter_domain>(&cat)
            ).first->second;
        }
    }

    auto make_error(std::error_code ec, adl::disappointment::type) noexcept
        -> error
    {
        static_assert(sizeof(int) <= sizeof(error_code));
        return {
            static_cast<error_code>(ec.value()),
            adapt_domain(ec.category())
        };
    }

    auto make_error(std::errc ec, adl::disappointment::type) noexcept
        -> error
    {
        return {
            static_cast<error_code>(ec),
            generic_cat_adapter
        };
    }
}

#if defined BOOST_OS_WINDOWS_AVAILABLE
#include <vefs/utils/windows-proper.h>
#elif defined BOOST_OS_LINUX_AVAILABLE || defined BOOST_OS_MACOS_AVAILABLE
#include <cerrno>
#endif

namespace vefs
{
    auto collect_system_error()
        -> std::error_code
    {
#if defined BOOST_OS_WINDOWS_AVAILABLE
        return std::error_code{ static_cast<int>(GetLastError()), std::system_category() };
#elif defined BOOST_OS_LINUX_AVAILABLE || defined BOOST_OS_MACOS_AVAILABLE
        return std::error_code{ errno, std::system_category() };
#endif
    }
}
