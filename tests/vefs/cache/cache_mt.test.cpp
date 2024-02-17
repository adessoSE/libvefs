#include "vefs/cache/cache_mt.hpp"

#include <boost/predef/compiler.h>
#include <vefs/cache/lru_policy.hpp>
#include <vefs/utils/workaround.h>

#include "boost-unit-test.hpp"
#include "test-utils.hpp"

using namespace vefs::detail;

namespace vefs_tests
{

struct immovable_value_type
{
    int value;
    bool *destructorCalled;

    ~immovable_value_type() noexcept
    {
        if (destructorCalled != nullptr)
        {
            *destructorCalled = true;
        }
    }

    immovable_value_type(immovable_value_type const &) = delete;
    auto operator=(immovable_value_type const &)
            -> immovable_value_type & = delete;

    immovable_value_type(int emplace, bool *destructedState = nullptr)
        : value(emplace)
        , destructorCalled(destructedState)
    {
        if (destructorCalled != nullptr)
        {
            *destructorCalled = false;
        }
    }

    friend inline auto operator<<(std::ostream &out,
                                  immovable_value_type const &wrapper)
            -> std::ostream &
    {
        return out << wrapper.value;
    }
};

struct ex_stats
{
    int syncCalled{0};
    int purgeCalled{0};
};

struct ex_traits
{
    using initializer_type = ex_stats *;
    using key_type = std::uint64_t;
    using value_type = immovable_value_type;
    struct load_context
    {
        int emplace;
        bool *destructorCalled;
    };
    struct purge_context
    {
    };
    using allocator_type = std::allocator<void>;
    using eviction = vefs::detail::
            least_recently_used_policy<key_type, std::uint32_t, allocator_type>;

    ex_stats *stats;
    ex_traits(ex_stats *st)
        : stats(st)
    {
    }

    auto load(load_context const &ctx,
              key_type,
              vefs::utils::object_storage<value_type> &storage) noexcept
            -> vefs::result<std::pair<value_type *, bool>>
    {
        auto cx = &storage.construct(ctx.emplace, ctx.destructorCalled);
        return std::pair{cx, false};
    }
    auto sync(key_type, value_type const &) noexcept -> vefs::result<void>
    {
        if (stats != nullptr)
        {
            stats->syncCalled += 1;
        }
        return vefs::success();
    }
    auto purge(purge_context const &, key_type, value_type &) noexcept
            -> vefs::result<void>
    {
        if (stats != nullptr)
        {
            stats->purgeCalled += 1;
        }
        return vefs::success();
    }
};
static_assert(vefs::detail::cache_traits<ex_traits>);

} // namespace vefs_tests

template class vefs::detail::cache_mt<vefs_tests::ex_traits>;

template class vefs::detail::cache_handle<uint64_t, uint32_t>;
#if VEFS_WORKAROUND_TESTED_AT(BOOST_COMP_CLANG, 16, 0, 6)
#else
static_assert(std::regular<cache_handle<uint64_t, uint32_t>>);
#endif
template class vefs::detail::cache_handle<uint64_t, uint32_t const>;
#if VEFS_WORKAROUND_TESTED_AT(BOOST_COMP_CLANG, 16, 0, 6)
#else
static_assert(std::regular<cache_handle<uint64_t, uint32_t const>>);
#endif

namespace vefs_tests
{
BOOST_AUTO_TEST_SUITE(cache_ng)

#if VEFS_WORKAROUND_TESTED_AT(BOOST_COMP_CLANG, 16, 0, 6)
#else
static_assert(requires(cache_handle<uint64_t, uint32_t> t,
                       cache_handle<uint64_t, uint32_t const> u) {
                  t == u;
                  t != u;
                  u == t;
                  u != t;
              });
#endif

BOOST_AUTO_TEST_CASE(default_ctor)
{
    [[maybe_unused]] cache_mt<ex_traits> subject(1024U, nullptr);
}

BOOST_AUTO_TEST_CASE(load_simple)
{
    constexpr std::uint32_t key = 1U;
    constexpr int beef = 0xbeef;
    cache_mt<ex_traits> subject(1024U, nullptr);

    auto const preloadResult = nullptr == subject.try_pin(key);
    BOOST_TEST(preloadResult);

    auto const loadrx = subject.pin_or_load({beef, nullptr}, key);
    TEST_RESULT_REQUIRE(loadrx);
    auto const loadedValue = loadrx.assume_value()->value;
    BOOST_TEST(loadedValue == beef);
}

BOOST_AUTO_TEST_CASE(upgrade_handle)
{
    constexpr std::uint32_t key = 1U;
    constexpr int beef = 0xbeef;
    constexpr int dead = 0xdead;
    cache_mt<ex_traits> subject(1024U, nullptr);

    auto const loadrx = subject.pin_or_load({beef, nullptr}, key);
    TEST_RESULT_REQUIRE(loadrx);
    BOOST_TEST(!loadrx.assume_value().is_dirty());

    {
        auto writableHandle = loadrx.assume_value().as_writable();
        BOOST_TEST(!loadrx.assume_value().is_dirty());
        writableHandle->value = dead;
    }
    BOOST_TEST(loadrx.assume_value().is_dirty());

    auto const loadedValue = loadrx.assume_value()->value;
    BOOST_TEST(loadedValue == dead);
}

BOOST_AUTO_TEST_CASE(purge_simple)
{
    ex_stats stats{};
    constexpr std::uint32_t key = 1U;
    constexpr int beef = 0xbeef;
    cache_mt<ex_traits> subject(1024U, &stats);

    bool destructorCalled = false;
    auto loadrx = subject.pin_or_load({beef, &destructorCalled}, key);
    TEST_RESULT_REQUIRE(loadrx);
    loadrx.assume_value() = nullptr;

    BOOST_TEST(stats.purgeCalled == 0);

    ex_traits::purge_context purgeContext;
    TEST_RESULT_REQUIRE(subject.purge(purgeContext, key));

    BOOST_TEST(stats.purgeCalled == 1);

    auto const purgeVerified = subject.try_pin(key) == nullptr;
    BOOST_TEST(purgeVerified);
    BOOST_TEST(destructorCalled);
}

BOOST_AUTO_TEST_CASE(auto_sync_on_dirty_eviction)
{
    int const max_entries = 64;
    ex_stats stats{};
    cache_mt<ex_traits> subject(
            max_entries + std::thread::hardware_concurrency() * 2, &stats);

    // mark LRU entry as dirty
    (void)subject.pin_or_load({0, nullptr}, 0U).value().as_writable();
    // fill cache
    for (int i = 1; i < max_entries; ++i)
    {
        TEST_RESULT_REQUIRE(
                subject.pin_or_load({i, nullptr}, static_cast<unsigned>(i)));
    }

    BOOST_TEST(stats.syncCalled == 0);

    // cause eviction of #0
    TEST_RESULT(subject.pin_or_load({max_entries, nullptr},
                                    static_cast<unsigned>(max_entries)));

    BOOST_TEST(stats.syncCalled == 1);
}

BOOST_AUTO_TEST_CASE(least_recently_used_entry_gets_evicted)
{
    bool destructorCalled = false;
    int const max_entries = 64;
    cache_mt<ex_traits> subject(
            max_entries + std::thread::hardware_concurrency() * 2, nullptr);

    for (int i = 0; i < max_entries; ++i)
    {
        TEST_RESULT_REQUIRE(
                subject.pin_or_load({i, i == 1 ? &destructorCalled : nullptr},
                                    static_cast<unsigned>(i)));
    }
    // make the first accessed/inserted entry the most recently used
    (void)subject.try_pin(0U);

    TEST_RESULT_REQUIRE(subject.pin_or_load(
            {max_entries, nullptr}, static_cast<unsigned>(max_entries)));

    auto const firstInsertedStillExists = subject.try_pin(0U) != nullptr;
    BOOST_TEST(firstInsertedStillExists);

    auto const secondInsertedHasBeenPurged = subject.try_pin(1U) == nullptr;
    BOOST_TEST(secondInsertedHasBeenPurged);
    BOOST_TEST(destructorCalled);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace vefs_tests
