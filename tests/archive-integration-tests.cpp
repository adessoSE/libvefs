#include <vefs/archive.hpp>
#include <vefs/platform/thread_pool.hpp>
#include <vefs/utils/random.hpp>

#include "vefs/detail/sector_device.hpp"

#include "test-utils.hpp"

using namespace std::string_literals;
using namespace std::string_view_literals;
using namespace vefs;

namespace
{
constexpr std::array<std::byte, 32> default_user_prk{};

constexpr auto default_file_path = "diupdope"sv;

struct archive_test_dependencies
{
    vefs::crypto::crypto_provider *cprov;
    std::string testFileName;
    vefs::archive_handle testSubject;

    archive_test_dependencies()
        : cprov(vefs::test::only_mac_crypto_provider())
        , testFileName(vefs::llfio::utils::random_string(8) + ".vefs"s)
    {
        testSubject = vefs::archive(
                              vefs_tests::current_path, testFileName,
                              default_user_prk, cprov,
                              vefs::archive_handle::creation::only_if_not_exist)
                              .value();
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(archive_integration_test, archive_test_dependencies)

BOOST_AUTO_TEST_CASE(sqlite_bridge_regression_1)
{
    using file_type = std::array<std::byte, 8192>;
    auto fileDataStorage = std::make_unique<file_type>();
    std::span fileData{*fileDataStorage};

    utils::xoroshiro128plus dataGenerator{0};

    auto fopenrx = testSubject.open("blob-test-journal",
                                    file_open_mode::readwrite
                                            | file_open_mode::create);
    TEST_RESULT_REQUIRE(fopenrx);
    auto f = std::move(fopenrx).assume_value();

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 0));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 8192));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 2 * 8192));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 3 * 8192));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 4 * 8192));

    TEST_RESULT_REQUIRE(testSubject.commit(f));
    f = nullptr;

    TEST_RESULT_REQUIRE(testSubject.erase("blob-test-journal"));

    fopenrx = testSubject.open("blob-test-journal",
                               file_open_mode::readwrite
                                       | file_open_mode::create);
    TEST_RESULT_REQUIRE(fopenrx);
    f = std::move(fopenrx).assume_value();

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 0));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 8192));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 2 * 8192));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 3 * 8192));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 4 * 8192));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 32772));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData.subspan(0, 4), 40964));
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData.subspan(4, 4), 40968));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 40972));

    dataGenerator.fill(fileData);
    TEST_RESULT_REQUIRE(testSubject.write(f, fileData.subspan(0, 4), 49164));

    TEST_RESULT_REQUIRE(testSubject.commit(f));
    f = nullptr;

    TEST_RESULT_REQUIRE(testSubject.erase("blob-test-journal"));
}

BOOST_AUTO_TEST_CASE(sqlite_bridge_regression_2)
{
    using namespace vefs;

    using file_type = std::array<std::byte, 0x1000>;
    auto fileDataStorage = std::make_unique<file_type>();
    std::span fileData{*fileDataStorage};
    vefs::fill_blob(fileData, std::byte{0x55});

    utils::xoroshiro128plus dataGenerator{0};

    {
        auto fopenrx = testSubject.open("db", file_open_mode::readwrite
                                                      | file_open_mode::create);
        TEST_RESULT_REQUIRE(fopenrx);
        auto f = std::move(fopenrx).assume_value();

        TEST_RESULT_REQUIRE(testSubject.commit(f));

        TEST_RESULT_REQUIRE(testSubject.commit());

        TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 0x00000000));

        for (int i = 0; i < 0xf6; ++i)
        {
            BOOST_TEST_INFO_SCOPE(i);

            TEST_RESULT_REQUIRE(
                    testSubject.write(f, fileData, 0x0000000ull + i * 0x1000u));
        }

        TEST_RESULT_REQUIRE(testSubject.commit(f));

        TEST_RESULT_REQUIRE(testSubject.commit());

        TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 0x0000b000));

        for (int j = 0; j < 98; ++j)
        {
            BOOST_TEST_INFO_SCOPE(j);

            TEST_RESULT_REQUIRE(testSubject.write(f, fileData,
                                                  0x000f5000ull + j * 0x1000u));
        }

        TEST_RESULT_REQUIRE(testSubject.commit(f));

        TEST_RESULT_REQUIRE(testSubject.commit());

        TEST_RESULT_REQUIRE(testSubject.write(f, fileData, 0x000f4000));

        for (int j = 0; j < 111; ++j)
        {
            BOOST_TEST_INFO_SCOPE(j);

            TEST_RESULT_REQUIRE(testSubject.write(f, fileData,
                                                  0x0010d000ull + j * 0x1000u));
        }

        TEST_RESULT_REQUIRE(testSubject.commit(f));
        f = nullptr;

        TEST_RESULT_REQUIRE(testSubject.commit());
        testSubject = {};
    }
    {
        auto validaterx = archive_handle::validate(vefs_tests::current_path,
                                                   testFileName,
                                                   default_user_prk, cprov);
        TEST_RESULT(validaterx);
    }
}

BOOST_AUTO_TEST_CASE(read_write_with_empty_prk_and_boringssl_provider)
{
    constexpr std::uint64_t pos
            = detail::sector_device::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    std::span file{*bigFile};

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

    auto fopenrx
            = testSubject.open(default_file_path, file_open_mode::readwrite);
    TEST_RESULT_REQUIRE(fopenrx);
    hFile = std::move(fopenrx).assume_value();

    BOOST_TEST_REQUIRE(testSubject.maximum_extent_of(hFile).value()
                       == file.size() + pos);

    auto readBuffer = std::make_unique<file_type>();
    TEST_RESULT_REQUIRE(testSubject.read(hFile, std::span{*readBuffer}, pos));
    auto readSpan = std::span{*readBuffer};

    BOOST_CHECK_EQUAL_COLLECTIONS(file.begin(), file.end(), readSpan.begin(),
                                  readSpan.end());
}

BOOST_AUTO_TEST_SUITE_END()
