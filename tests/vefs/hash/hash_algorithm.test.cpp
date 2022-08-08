#include "vefs/hash/hash_algorithm.hpp"

#include <tuple>

#include "boost-unit-test.hpp"

namespace vefs_tests::hash_algorithm_tmp
{

enum class non_trivially_hashable : std::uint32_t
{
};

}

template <>
inline constexpr bool vefs::disable_trivially_hashable<
        vefs_tests::hash_algorithm_tmp::non_trivially_hashable> = true;

namespace vefs_tests
{

BOOST_AUTO_TEST_SUITE(hash_algorithm_tmp)

struct test_hash
{
    std::tuple<std::byte const *, std::size_t> updateArgs = {};
    void update(std::byte const *data, std::size_t size) noexcept
    {
        updateArgs = {data, size};
    }
    template <dplx::cncr::unsigned_integer H>
    auto final() noexcept -> H
    {
        return {};
    }

    static std::tuple<std::byte const *, std::size_t> hashArgs;
    template <dplx::cncr::unsigned_integer H>
    static auto hash(std::byte const *data, std::size_t size) noexcept -> H
    {
        hashArgs = {data, size};
        return {};
    }
};
std::tuple<std::byte const *, std::size_t> test_hash::hashArgs = {};

static_assert(vefs::hash_algorithm<test_hash>);
static_assert(!vefs::keyable_hash_algorithm<test_hash>);

struct keyed_test_hash
{
    keyed_test_hash() noexcept = default;

    struct key_type
    {
        std::size_t off;
    };

    explicit keyed_test_hash(key_type const &) noexcept
    {
    }

    static auto generate_key() noexcept -> key_type
    {
        return {};
    }
    static void generate_keys(std::span<key_type> out) noexcept
    {
        std::ranges::fill(out, key_type{});
    }

    std::tuple<std::byte const *, std::size_t> updateArgs = {};
    void update(std::byte const *data, std::size_t size) noexcept
    {
        updateArgs = {data, size};
    }
    template <dplx::cncr::unsigned_integer H>
    auto final() noexcept -> H
    {
        return {};
    }

    static std::tuple<std::byte const *, std::size_t> hashArgs;
    template <dplx::cncr::unsigned_integer H>
    static auto hash(std::byte const *data, std::size_t size) noexcept -> H
    {
        hashArgs = {data, size};
        return {};
    }
    static std::tuple<std::byte const *, std::size_t> keyedHashArgs;
    template <dplx::cncr::unsigned_integer H>
    static auto hash(key_type const &k,
                     std::byte const *data,
                     std::size_t size) noexcept -> H
    {
        (void)k;
        keyedHashArgs = {data, size};
        return {};
    }
};
std::tuple<std::byte const *, std::size_t> keyed_test_hash::hashArgs = {};
std::tuple<std::byte const *, std::size_t> keyed_test_hash::keyedHashArgs = {};

static_assert(vefs::hash_algorithm<keyed_test_hash>);
static_assert(vefs::keyable_hash_algorithm<keyed_test_hash>);

// test the hashable concept for trivially hashable types
static_assert(vefs::hashable<char, test_hash>);
static_assert(vefs::hashable<unsigned char, test_hash>);
static_assert(vefs::hashable<signed char, test_hash>);
static_assert(vefs::hashable<short, test_hash>);
static_assert(vefs::hashable<unsigned short, test_hash>);
static_assert(vefs::hashable<int, test_hash>);
static_assert(vefs::hashable<unsigned int, test_hash>);
static_assert(vefs::hashable<long, test_hash>);
static_assert(vefs::hashable<unsigned long, test_hash>);
static_assert(vefs::hashable<long long, test_hash>);
static_assert(vefs::hashable<unsigned long long, test_hash>);

static_assert(vefs::hashable<char *, test_hash>);
static_assert(vefs::hashable<int *, test_hash>);
static_assert(vefs::hashable<char const *, test_hash>);
static_assert(vefs::hashable<int const *, test_hash>);

static_assert(!vefs::trivially_hashable<non_trivially_hashable>);
static_assert(!vefs::hashable<non_trivially_hashable, test_hash>);
static_assert(!vefs::hashable<test_hash, test_hash>);

BOOST_AUTO_TEST_CASE(hash_update_call)
{
    unsigned value = {};
    test_hash subject = {};

    vefs::hash_update(subject, value);
    BOOST_TEST(
            !!(subject.updateArgs
               == std::tuple<std::byte const *, std::size_t>{
                       reinterpret_cast<std::byte *>(&value), sizeof(value)}));
}

BOOST_AUTO_TEST_CASE(hash_call)
{
    unsigned value = {};

    std::uint64_t h = vefs::hash<test_hash, std::uint64_t>(value);
    BOOST_TEST(h == 0U);
    BOOST_TEST(
            !!(test_hash::hashArgs
               == std::tuple<std::byte const *, std::size_t>{
                       reinterpret_cast<std::byte *>(&value), sizeof(value)}));
}

BOOST_AUTO_TEST_CASE(std_hash_for_call)
{
    unsigned value = {};

    std::size_t h = vefs::std_hash_for<test_hash, unsigned>()(value);
    BOOST_TEST(h == 0U);
    BOOST_TEST(
            !!(test_hash::hashArgs
               == std::tuple<std::byte const *, std::size_t>{
                       reinterpret_cast<std::byte *>(&value), sizeof(value)}));
}

BOOST_AUTO_TEST_CASE(keyed_hash_update_call)
{
    unsigned value = {};
    keyed_test_hash subject = {};

    vefs::hash_update(subject, value);
    BOOST_TEST(
            !!(subject.updateArgs
               == std::tuple<std::byte const *, std::size_t>{
                       reinterpret_cast<std::byte *>(&value), sizeof(value)}));
}

BOOST_AUTO_TEST_CASE(keyed_hash_call)
{
    unsigned value = {};

    std::uint64_t h = vefs::hash<keyed_test_hash, std::uint64_t>(value);
    BOOST_TEST(h == 0U);
    BOOST_TEST(
            !!(keyed_test_hash::hashArgs
               == std::tuple<std::byte const *, std::size_t>{
                       reinterpret_cast<std::byte *>(&value), sizeof(value)}));
}

BOOST_AUTO_TEST_CASE(keyed_hash_call2)
{
    unsigned value = {};

    std::uint64_t h = vefs::hash<keyed_test_hash, std::uint64_t>(
            keyed_test_hash::key_type{}, value);
    BOOST_TEST(h == 0U);
    BOOST_TEST(
            !!(keyed_test_hash::keyedHashArgs
               == std::tuple<std::byte const *, std::size_t>{
                       reinterpret_cast<std::byte *>(&value), sizeof(value)}));
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace vefs_tests
