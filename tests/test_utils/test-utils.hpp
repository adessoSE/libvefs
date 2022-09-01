#pragma once
#include <ostream>
#include <string_view>
#include "boost-unit-test.hpp"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <vefs/disappointment.hpp>
#include <vefs/utils/random.hpp>
#include "libb2_none_blake2b_crypto_provider.hpp"
#include "vefs/detail/file_crypto_ctx.hpp"

struct test_rng : vefs::utils::xoroshiro128plus
{
    // default initialize the test rng to the first 32 hex digits of pi
    // pi is random enough to be a good seed and hard coding it here
    // guarantees that the test cases are reproducible
    test_rng()
        : xoroshiro128plus(0x243F'6A88'85A3'08D3ull, 0x1319'8A2E'0370'7344ull)
    {
    }
    using xoroshiro128plus::xoroshiro128plus;
};

namespace vefs
{
inline std::ostream &operator<<(std::ostream &s, error_domain const &domain)
{
    using namespace fmt::literals;
    fmt::print(s, "[error_domain|{}]", domain.name());
    return s;
}

template <typename T>
inline auto check_result(result<T> const &rx)
        -> boost::test_tools::predicate_result
{
    if (!rx)
    {
        boost::test_tools::predicate_result prx{false};
        prx.message() << rx.assume_error();
        return prx;
    }
    return true;
}
} // namespace vefs

#define TEST_RESULT(...) BOOST_TEST((::vefs::check_result((__VA_ARGS__))))
#define TEST_RESULT_REQUIRE(...)                                               \
    BOOST_TEST_REQUIRE((::vefs::check_result((__VA_ARGS__))))

namespace std
{

inline auto boost_test_print_type(std::ostream &s, std::byte b)
        -> std::ostream &
{
    fmt::print(s, FMT_STRING("{:x}"), static_cast<std::uint8_t>(b));
    return s;
}

} // namespace std

namespace vefs_tests
{

extern vefs::llfio::path_handle const current_path;

}
