#include <vefs/disappointment.hpp>
#include "boost-unit-test.hpp"

#include "test-utils.hpp"

BOOST_AUTO_TEST_SUITE(disappointment_tests)

using vefs::error_info;
using vefs::error_detail;
using vefs::error_domain;
using vefs::archive_domain;


BOOST_AUTO_TEST_CASE(error_info_format)
{
    using namespace std::string_view_literals;
    using fmt::format;

    error_info info;
    BOOST_TEST(format(fmt("{:v}"), info) == ""sv);
}


BOOST_AUTO_TEST_SUITE_END()
