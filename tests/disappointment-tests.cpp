#include <vefs/disappointment.hpp>
#include "boost-unit-test.hpp"

#include <vefs/exceptions.hpp>

#include "test-utils.hpp"

BOOST_AUTO_TEST_SUITE(disappointment_tests)

using vefs::errc;
using vefs::error;
using vefs::error_info;
using vefs::error_detail;
using vefs::error_domain;

BOOST_AUTO_TEST_CASE(error_default_initialization)
{
    using namespace std::string_view_literals;
    error edefault;

    BOOST_TEST(edefault.code() == 0);
    BOOST_TEST(!edefault.has_info());
    BOOST_TEST(!edefault);
    BOOST_TEST(edefault.domain().name() == "success-domain"sv);
}

BOOST_AUTO_TEST_CASE(error_manual_initialization)
{
    constexpr vefs::error_code val{ 0xC0DEDDEADBEAF };
    static_assert(((val << 1) >> 1) == val);
    error e{ 0xC0DEDDEADBEAF, vefs::generic_domain() };

    BOOST_TEST(e.code() == val);
    BOOST_CHECK(e.domain() == vefs::generic_domain());
    BOOST_TEST(!e.has_info());
    BOOST_TEST(e);
}

BOOST_AUTO_TEST_CASE(error_code_initialization)
{
    error e{ errc::result_out_of_range };

    BOOST_TEST(e.code() == static_cast<vefs::error_code>(errc::result_out_of_range));
    BOOST_CHECK(e.domain() == vefs::generic_domain());
    BOOST_TEST(!e.has_info());
    BOOST_TEST(e);
}

BOOST_AUTO_TEST_CASE(error_comparison)
{
    using vefs::archive_errc;
    error l{ errc::invalid_argument };
    error r{ archive_errc::invalid_prefix };

    BOOST_TEST(l == errc::invalid_argument);
    BOOST_TEST(l != errc::key_already_exists);
    BOOST_TEST(l != r);
    BOOST_TEST(archive_errc::identical_header_version != r);
    BOOST_TEST(archive_errc::invalid_prefix == r);
}

BOOST_AUTO_TEST_CASE(error_info_allocation)
{
    error e;
    BOOST_TEST_REQUIRE(!e.has_info());
    BOOST_TEST_REQUIRE(e.ensure_allocated() == error{});
    BOOST_TEST(e.has_info());
    e.info();
}

BOOST_AUTO_TEST_CASE(error_format)
{
    using namespace std::string_view_literals;
    using namespace fmt::literals;
    using fmt::format;

    error info;

    // require compile time parsing
    format(FMT_STRING("{}"), info);
    format(FMT_STRING("{:v}"), info);
    format(FMT_STRING("{:!v}"), info);

    BOOST_TEST("{}"_format(info) == "success-domain => success"sv);
}

BOOST_AUTO_TEST_CASE(error_format_w_details)
{
    using namespace std::string_view_literals;
    using namespace fmt::literals;
    using fmt::format;

    error info{ vefs::archive_errc::tag_mismatch };
    info << vefs::ed::error_code_api_origin{ "xyz-xapi()"sv };

    BOOST_TEST("{}"_format(info) == "vefs-archive-domain => decryption failed because the message tag didn't match\n"
        "\t[enum vefs::ed::error_code_origin_tag] = xyz-xapi()"sv);
}

BOOST_AUTO_TEST_CASE(std_error_code_adaption)
{
    std::error_code ec = make_error_code(std::errc::message_size);
    error conv{ ec };

    BOOST_TEST(static_cast<int>(conv.code()) == ec.value());
}

BOOST_AUTO_TEST_SUITE_END()