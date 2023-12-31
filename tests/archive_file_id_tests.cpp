#include "vefs/detail/archive_file_id.hpp"

#include "boost-unit-test.hpp"
#include "test-utils.hpp"

BOOST_AUTO_TEST_SUITE(archive_file_id_tests)

BOOST_AUTO_TEST_CASE(archive_file_id_stringify)
{
    using namespace std::string_view_literals;
    using vefs::detail::file_id;

    file_id testId{
            vefs::uuid{{0xc7, 0xa5, 0x3d, 0x7a, 0xa4, 0xf0, 0x40, 0x53, 0xa7,
                        0xa3, 0x35, 0xf3, 0x5c, 0xdf, 0x53, 0x3d}}};

    BOOST_TEST(fmt::format("{}", testId)
               == "c7a53d7a-a4f0-4053-a7a3-35f35cdf533d"sv);
}

BOOST_AUTO_TEST_SUITE_END()
