#include <vefs/archive.hpp>
#include <vefs/platform/thread_pool.hpp>
#include <vefs/utils/random.hpp>

#include "../src/detail/sector_device.hpp"

#include "test-utils.hpp"

using namespace std::string_literals;
using namespace std::string_view_literals;
using namespace vefs;

namespace
{
constexpr std::array<std::byte, 32> default_user_prk{};
static_assert(default_user_prk.size() == 32);

constexpr auto default_file_path = "diupdope"sv;

struct basic_dependencies
{
    vefs::crypto::crypto_provider *cprov;
    std::string testFileName;

    basic_dependencies()
        : cprov(vefs::test::only_mac_crypto_provider())
        , testFileName(vefs::llfio::utils::random_string(8) + ".vefs"s)
    {
    }
};
struct archive_test_dependencies : basic_dependencies
{
    vefs::archive_handle testSubject;

    archive_test_dependencies()
    {
        testSubject = vefs::archive(
                              vefs_tests::current_path, testFileName,
                              default_user_prk, cprov,
                              vefs::archive_handle::creation::only_if_not_exist)
                              .value();
    }
    ~archive_test_dependencies()
    {
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(archive_tests, archive_test_dependencies)

BOOST_FIXTURE_TEST_CASE(archive_create, basic_dependencies)
{
    auto openrx = vefs::archive(
            vefs_tests::current_path, testFileName, default_user_prk, cprov,
            vefs::archive_handle::creation::only_if_not_exist);
    TEST_RESULT_REQUIRE(openrx);
    TEST_RESULT(openrx.assume_value().commit());
}

BOOST_AUTO_TEST_CASE(reopen_archive_succeeds)
{
    TEST_RESULT_REQUIRE(testSubject.commit());
    testSubject = {};

    auto openrx = vefs::archive(vefs_tests::current_path, testFileName,
                                default_user_prk, cprov,
                                vefs::archive_handle::creation::open_existing);
    TEST_RESULT(openrx);
}

BOOST_AUTO_TEST_CASE(reopen_keeps_created_files)
{
    TEST_RESULT_REQUIRE(testSubject.commit());
    constexpr std::uint64_t pos
            = detail::sector_device::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    span writeContent{*bigFile};

    utils::xoroshiro128plus dataGenerator{0xC0DE'DEAD'BEEF'3ABA};
    dataGenerator.fill(writeContent);

    auto fileOpenRx = testSubject.open(default_file_path,
                                       file_open_mode::readwrite
                                               | file_open_mode::create);
    TEST_RESULT_REQUIRE(fileOpenRx);
    auto file = std::move(fileOpenRx).assume_value();

    TEST_RESULT(testSubject.write(file, writeContent, pos));
    TEST_RESULT_REQUIRE(testSubject.commit(file));
    TEST_RESULT_REQUIRE(testSubject.commit());

    testSubject = {};
    auto openrx = vefs::archive(vefs_tests::current_path, testFileName,
                                default_user_prk, cprov,
                                vefs::archive_handle::creation::open_existing);
    TEST_RESULT_REQUIRE(openrx);
    testSubject = std::move(openrx).assume_value();

    fileOpenRx = testSubject.open(default_file_path, file_open_mode::read);
    TEST_RESULT_REQUIRE(fileOpenRx);
    file = std::move(fileOpenRx).assume_value();

    auto readBuffer = std::make_unique<file_type>();
    TEST_RESULT(testSubject.read(file, span{*readBuffer}, pos));

    auto readSpan = span{*readBuffer};

    BOOST_CHECK_EQUAL_COLLECTIONS(writeContent.cbegin(), writeContent.cend(),
                                  readSpan.cbegin(), readSpan.cend());
}

BOOST_AUTO_TEST_CASE(archive_cannot_be_opened_parallel)
{
    TEST_RESULT_REQUIRE(testSubject.commit());

    vefs::llfio::log_level_guard guard(vefs::llfio::log_level::none);
    auto reopenrx = vefs::archive(
            vefs_tests::current_path, testFileName, default_user_prk, cprov,
            vefs::archive_handle::creation::open_existing);

    vefs::llfio::log_level_guard reset_guard(vefs::llfio::log_level::all);

    BOOST_TEST(reopenrx.error() == errc::still_in_use);
}

BOOST_AUTO_TEST_CASE(create_a_new_file_succeeds)
{
    auto fileRx = testSubject.open(default_file_path,
                                   file_open_mode::readwrite
                                           | file_open_mode::create);
    TEST_RESULT_REQUIRE(fileRx);
    TEST_RESULT_REQUIRE(testSubject.commit(fileRx.assume_value()));
    TEST_RESULT_REQUIRE(testSubject.commit());

    auto reopenedFile
            = testSubject.open(default_file_path, file_open_mode::read);
    TEST_RESULT_REQUIRE(reopenedFile);
}

BOOST_AUTO_TEST_CASE(read_content_that_was_written)
{
    constexpr std::uint64_t pos
            = detail::sector_device::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    span writeContent{*bigFile};

    utils::xoroshiro128plus dataGenerator{0xC0DE'DEAD'BEEF'3ABA};
    dataGenerator.fill(writeContent);

    auto fileOpenRx = testSubject.open(default_file_path,
                                       file_open_mode::readwrite
                                               | file_open_mode::create);
    TEST_RESULT_REQUIRE(fileOpenRx);
    auto file = std::move(fileOpenRx).assume_value();

    TEST_RESULT(testSubject.write(file, writeContent, pos));
    TEST_RESULT_REQUIRE(testSubject.commit(file));
    TEST_RESULT_REQUIRE(testSubject.commit());

    fileOpenRx = testSubject.open(default_file_path, file_open_mode::read);
    TEST_RESULT_REQUIRE(fileOpenRx);
    file = std::move(fileOpenRx).assume_value();

    auto readBuffer = std::make_unique<file_type>();
    TEST_RESULT(testSubject.read(file, span{*readBuffer}, pos));

    auto readSpan = span{*readBuffer};

    BOOST_CHECK_EQUAL_COLLECTIONS(writeContent.cbegin(), writeContent.cend(),
                                  readSpan.cbegin(), readSpan.cend());
}

BOOST_AUTO_TEST_CASE(archive_file_shrink)
{
    constexpr std::uint64_t pos
            = detail::sector_device::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    span writeContent{*bigFile};

    utils::xoroshiro128plus dataGenerator{0};
    dataGenerator.fill(writeContent);
    auto fileOpenRx = testSubject.open(default_file_path,
                                       file_open_mode::readwrite
                                               | file_open_mode::create);
    TEST_RESULT_REQUIRE(fileOpenRx);
    auto hFile = std::move(fileOpenRx).assume_value();

    TEST_RESULT(testSubject.write(hFile, writeContent, pos));
    TEST_RESULT_REQUIRE(testSubject.commit(hFile));
    TEST_RESULT_REQUIRE(testSubject.commit());

    auto fopenrx
            = testSubject.open(default_file_path, file_open_mode::readwrite);
    TEST_RESULT_REQUIRE(fopenrx);
    hFile = std::move(fopenrx).assume_value();

    TEST_RESULT(testSubject.truncate(
            hFile, 2 * detail::sector_device::sector_payload_size));

    TEST_RESULT_REQUIRE(testSubject.commit(hFile));
    TEST_RESULT_REQUIRE(testSubject.commit());

    fopenrx = testSubject.open(default_file_path, file_open_mode::readwrite);
    TEST_RESULT_REQUIRE(fopenrx);
    hFile = std::move(fopenrx).assume_value();

    TEST_RESULT(testSubject.truncate(hFile, 0));

    TEST_RESULT_REQUIRE(testSubject.commit(hFile));
    TEST_RESULT_REQUIRE(testSubject.commit());

    auto readBuffer = std::make_unique<file_type>();
    auto read_result = testSubject.read(hFile, span{*readBuffer}, pos);
    BOOST_TEST_REQUIRE(!testSubject.read(hFile, span{*readBuffer}, pos));
    BOOST_TEST(read_result.assume_error()
               == archive_errc::sector_reference_out_of_range);
}

BOOST_AUTO_TEST_CASE(erased_file_cannot_be_queried)
{
    constexpr std::uint64_t pos
            = detail::sector_device::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    span file{*bigFile};

    utils::xoroshiro128plus dataGenerator{0};
    dataGenerator.fill(file);

    auto fileOpenRx = testSubject.open(default_file_path,
                                       file_open_mode::readwrite
                                               | file_open_mode::create);
    TEST_RESULT_REQUIRE(fileOpenRx);
    auto hFile = std::move(fileOpenRx).assume_value();

    TEST_RESULT(testSubject.write(hFile, file, pos));

    TEST_RESULT_REQUIRE(testSubject.commit(hFile));
    TEST_RESULT_REQUIRE(testSubject.commit());
    hFile.reset();

    TEST_RESULT(testSubject.erase(default_file_path));

    TEST_RESULT_REQUIRE(testSubject.commit());

    auto queryRx = testSubject.query(default_file_path);
    BOOST_REQUIRE(queryRx.has_error()
                  && queryRx.assume_error() == archive_errc::no_such_file);
}

BOOST_AUTO_TEST_CASE(query_cannot_find_non_existing_file)
{
    auto result = testSubject.query(
            "somerandomfilename/asdflsdfmasfw/sadfaöjksdfn");
    BOOST_REQUIRE(!result);
    BOOST_TEST(result.error() == archive_errc::no_such_file);
}

BOOST_AUTO_TEST_CASE(query_finds_existing_file)
{
    constexpr std::uint64_t pos
            = detail::sector_device::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    span file{*bigFile};

    utils::xoroshiro128plus dataGenerator{0};
    dataGenerator.fill(file);

    auto fileOpenRx = testSubject.open(default_file_path,
                                       file_open_mode::readwrite
                                               | file_open_mode::create);
    TEST_RESULT_REQUIRE(fileOpenRx);
    auto hFile = std::move(fileOpenRx).assume_value();

    TEST_RESULT(testSubject.write(hFile, file, pos));

    BOOST_TEST_REQUIRE(testSubject.maximum_extent_of(hFile).value()
                       == file.size() + pos);

    TEST_RESULT_REQUIRE(testSubject.commit(hFile));
    TEST_RESULT_REQUIRE(testSubject.commit());

    auto result = testSubject.query(default_file_path);
    BOOST_TEST_REQUIRE(result.has_value());

    BOOST_TEST(result.assume_value().size == file.size() + pos);
}

BOOST_AUTO_TEST_CASE(personalization_round_trips)
{
    std::array<std::byte,
               vefs::detail::sector_device::personalization_area_size>
            personalization;
    for (std::size_t i = 0; i < personalization.size(); ++i)
    {
        personalization[i] = static_cast<std::byte>(i);
    }

    {
        vefs::copy(std::span(personalization),
                   testSubject.personalization_area());
        auto syncRx = testSubject.sync_personalization_area();
        TEST_RESULT_REQUIRE(syncRx);

        TEST_RESULT_REQUIRE(testSubject.commit());
    }

    testSubject = {};

    std::array<std::byte,
               vefs::detail::sector_device::personalization_area_size>
            readContent{};
    {

        auto readRx = vefs::read_archive_personalization_area(
                vefs_tests::current_path, testFileName, readContent);
        TEST_RESULT_REQUIRE(readRx);
    }

    BOOST_CHECK_EQUAL_COLLECTIONS(personalization.cbegin(),
                                  personalization.cend(), readContent.cbegin(),
                                  readContent.cend());
}

BOOST_AUTO_TEST_SUITE_END()
