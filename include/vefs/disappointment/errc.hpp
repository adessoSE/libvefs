#pragma once

#include <dplx/predef/compiler.h>

#include <status-code/error.hpp>
#include <status-code/system_code.hpp>

#if defined(DPLX_COMP_GNUC_AVAILABLE)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif

namespace vefs
{

namespace system_error = SYSTEM_ERROR2_NAMESPACE;

enum class archive_errc : int
{
    success = 0,
    invalid_prefix = 1,
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

class archive_domain_type;
using archive_code = system_error::status_code<archive_domain_type>;

class archive_domain_type : public system_error::status_code_domain
{
    using base = system_error::status_code_domain;
    template <class DomainType>
    friend class system_error::status_code;

public:
    static constexpr std::string_view uuid
            = "9F10BF2E-4F20-459E-9976-4D975CBB3349";

    constexpr ~archive_domain_type() noexcept = default;
    constexpr archive_domain_type() noexcept
        : base(uuid.data(), base::_uuid_size<uuid.size()>{})
    {
    }
    constexpr archive_domain_type(archive_domain_type const &) noexcept
            = default;
    constexpr auto operator=(archive_domain_type const &) noexcept
            -> archive_domain_type & = default;

    using value_type = archive_errc;
    using base::string_ref;

    [[nodiscard]] constexpr auto name() const noexcept -> string_ref override
    {
        return string_ref("vefs-domain");
    }
    [[nodiscard]] constexpr auto payload_info() const noexcept
            -> payload_info_t override
    {
        return {sizeof(value_type),
                sizeof(value_type) + sizeof(archive_domain_type *),
                std::max(alignof(value_type), alignof(archive_domain_type *))};
    }

    static constexpr auto get() noexcept -> archive_domain_type const &;

protected:
    [[nodiscard]] constexpr auto
    _do_failure(system_error::status_code<void> const &code) const noexcept
            -> bool override
    {
        return static_cast<archive_code const &>(code).value()
               != archive_errc::success;
    }

    [[nodiscard]] constexpr auto
    map_to_generic(value_type const value) const noexcept -> system_error::errc
    {
        using enum archive_errc;
        using sys_errc = system_error::errc;
        switch (value)
        {
        case success:
            return sys_errc::success;

        case invalid_prefix:
        case oversized_static_header:
        case no_archive_header:
        case identical_header_version:
        case tag_mismatch:
        case sector_reference_out_of_range:
        case corrupt_index_entry:
        case vfilesystem_invalid_size:
            return sys_errc::bad_message;

        case no_such_vfile:
            return sys_errc::no_such_file_or_directory;

        case wrong_user_prk:
            return sys_errc::invalid_argument;

        case archive_file_already_existed:
            return sys_errc::file_exists;

        case archive_file_did_not_exist:
            return sys_errc::no_such_file_or_directory;

        default:
            return sys_errc::unknown;
        }
    }

    [[nodiscard]] constexpr auto
    map_to_message(value_type const value) const noexcept -> std::string_view
    {
        using enum archive_errc;
        using namespace std::string_view_literals;

        switch (value)
        {
        case invalid_prefix:
            return "the magic number at the beginning of the archive didn't match"sv;

        case oversized_static_header:
            return "the static archive header would be greater than the master sector"sv;

        case no_archive_header:
            return "no valid archive header could be read"sv;

        case identical_header_version:
            return "both archive headers were valid and contained the same version switch"sv;

        case tag_mismatch:
            return "decryption failed because the message tag didn't match"sv;

        case sector_reference_out_of_range:
            return "a sector reference pointed to a sector which currently isn't allocated"sv;

        case corrupt_index_entry:
            return "an entry from the archive index is corrupted and could not be read"sv;

        case no_such_vfile:
            return "no file has been found under the given name"sv;

        case wrong_user_prk:
            return "the given archive key is not valid for this archive or the archive head has been corrupted"sv;

        case vfilesystem_invalid_size:
            return "the vfilesystem storage extent is not a multiple of the sector_payload_size"sv;

        case archive_file_already_existed:
            return "the given file already contained data which would be overwritten, but creation::only_if_not_exist was specified"sv;

        case archive_file_did_not_exist:
            return "the given file contained no data, but creation::open_existing"sv;

        default:
            return "unknown vefs archive error code"sv;
        }
    }

    [[nodiscard]] constexpr auto
    _do_equivalent(system_error::status_code<void> const &lhs,
                   system_error::status_code<void> const &rhs) const noexcept
            -> bool override
    {
        auto const &alhs = static_cast<archive_code const &>(lhs);
        if (rhs.domain() == *this)
        {
            return alhs.value()
                   == static_cast<archive_code const &>(rhs).value();
        }
        if (rhs.domain() == system_error::generic_code_domain)
        {
            system_error::errc sysErrc
                    = static_cast<system_error::generic_code const &>(rhs)
                              .value();

            return system_error::errc::unknown != sysErrc
                   && map_to_generic(alhs.value()) == sysErrc;
        }
        return false;
    }
    [[nodiscard]] constexpr auto
    _generic_code(system_error::status_code<void> const &code) const noexcept
            -> system_error::generic_code override
    {
        return map_to_generic(static_cast<archive_code const &>(code).value());
    }

    [[nodiscard]] constexpr auto
    _do_message(system_error::status_code<void> const &code) const noexcept
            -> string_ref override
    {
        auto const archiveCode = static_cast<archive_code const &>(code);
        auto const message = map_to_message(archiveCode.value());
        return string_ref(message.data(), message.size());
    }

    SYSTEM_ERROR2_NORETURN void _do_throw_exception(
            system_error::status_code<void> const &code) const override
    {
        throw system_error::status_error<archive_domain_type>(
                static_cast<archive_code const &>(code).clone());
    }
};
inline constexpr archive_domain_type archive_domain{};

constexpr auto archive_domain_type::get() noexcept
        -> archive_domain_type const &
{
    return archive_domain;
}

constexpr auto make_status_code(archive_errc c) noexcept -> archive_code
{
    return archive_code(system_error::in_place, c);
}

} // namespace vefs

#if defined(DPLX_COMP_GNUC_AVAILABLE)
#pragma GCC diagnostic pop
#endif
