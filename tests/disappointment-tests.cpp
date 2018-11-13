#include <vefs/disappointment.hpp>
#include "boost-unit-test.hpp"

#include <vefs/exceptions.hpp>

#include "test-utils.hpp"

BOOST_AUTO_TEST_SUITE(disappointment_tests)

using vefs::error_info;
using vefs::error_detail;
using vefs::error_domain;
using vefs::archive_domain;


BOOST_AUTO_TEST_CASE(error_info_format)
{
    using namespace std::string_view_literals;
    using namespace fmt::literals;
    using fmt::format;

    error_info info;

    // require compile time parsing
    format(FMT_STRING("{}"), info);
    format(FMT_STRING("{:v}"), info);
    format(FMT_STRING("{:!v}"), info);

    BOOST_TEST("{}"_format(info) == "success-domain => success"sv);
}

BOOST_AUTO_TEST_CASE(error_info_format_w_details)
{
    using namespace std::string_view_literals;
    using namespace fmt::literals;
    using fmt::format;

    error_info info{ vefs::archive_errc::tag_mismatch };
    info << vefs::ed::error_code_api_origin{ "xyz-xapi()"sv };

    BOOST_TEST("{}"_format(info) == "vefs-archive => decryption failed because the message tag didn't match\n"
        "\t[enum vefs::ed::error_code_origin_tag] = xyz-xapi()"sv);
}

BOOST_AUTO_TEST_SUITE_END()
