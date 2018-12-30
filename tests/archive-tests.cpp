#include <vefs/archive.hpp>
#include "boost-unit-test.hpp"

#include <vefs/detail/raw_archive.hpp>
#include <vefs/crypto/provider.hpp>
#include <vefs/utils/random.hpp>
#include "memfs.hpp"

#include "test-utils.hpp"

using namespace std::string_view_literals;
using namespace vefs::blob_literals;
constexpr auto default_user_prk_raw
    = 0x0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000_as_bytes;
constexpr auto default_user_prk = vefs::blob_view{ default_user_prk_raw };
static_assert(default_user_prk.size() == 32);

constexpr auto default_archive_path = "./test-archive.vefs"sv;
constexpr auto default_file_path = "diupdope"sv;

BOOST_AUTO_TEST_SUITE(vefs_archive_tests)

BOOST_AUTO_TEST_CASE(archive_create)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::debug_crypto_provider();

    auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::create);
    TEST_RESULT(openrx);
}

BOOST_AUTO_TEST_CASE(archive_create_reopen)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::debug_crypto_provider();

    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::create);
        TEST_RESULT_REQUIRE(openrx);
    }
    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::readwrite);
        TEST_RESULT(openrx);
    }
}

BOOST_AUTO_TEST_CASE(archive_create_file)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::debug_crypto_provider();

    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fopenrx = ac->open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fopenrx);
    }
    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fopenrx = ac->open(default_file_path, file_open_mode::read);
        TEST_RESULT_REQUIRE(fopenrx);
    }
}

BOOST_AUTO_TEST_CASE(archive_readwrite)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    constexpr std::uint64_t pos = detail::raw_archive::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    blob file{ *bigFile };

    utils::xoroshiro128plus dataGenerator{ 0 };
    dataGenerator.fill(file);

    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::create);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx = ac->open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        TEST_RESULT(ac->write(hFile, file, pos));
    }
    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx = ac->open(default_file_path, file_open_mode::read);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        auto readBuffer = std::make_unique<file_type>();
        TEST_RESULT(ac->read(hFile, blob{ *readBuffer }, pos));

        BOOST_TEST(mismatch(file, blob_view{ *readBuffer }) == file.size());
    }
}


BOOST_AUTO_TEST_CASE(archive_file_shrink)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    constexpr std::uint64_t pos = detail::raw_archive::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    blob file{ *bigFile };

    utils::xoroshiro128plus dataGenerator{ 0 };
    dataGenerator.fill(file);

    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::create);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx = ac->open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        TEST_RESULT(ac->write(hFile, file, pos));
    }
    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fopenrx = ac->open(default_file_path, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(fopenrx);
        auto hFile = std::move(fopenrx).assume_value();

        TEST_RESULT(ac->resize(hFile, 2 * detail::raw_archive::sector_payload_size));
    }
    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fopenrx = ac->open(default_file_path, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(fopenrx);
        auto hFile = std::move(fopenrx).assume_value();

        TEST_RESULT(ac->resize(hFile, 0));
    }
}

BOOST_AUTO_TEST_CASE(archive_file_erase)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    constexpr std::uint64_t pos = detail::raw_archive::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    blob file{ *bigFile };

    utils::xoroshiro128plus dataGenerator{ 0 };
    dataGenerator.fill(file);

    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::create);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx = ac->open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        TEST_RESULT(ac->write(hFile, file, pos));
    }
    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();

        TEST_RESULT(ac->erase(default_file_path));
    }
}

BOOST_AUTO_TEST_CASE(archive_empty_userprk)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    constexpr std::uint64_t pos = detail::raw_archive::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    blob file{ *bigFile };

    utils::xoroshiro128plus dataGenerator{ 0 };
    dataGenerator.fill(file);

    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::create);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx = ac->open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        TEST_RESULT(ac->write(hFile, file, pos));

        BOOST_TEST_REQUIRE(ac->size_of(hFile).value() == file.size() + pos);
    }
    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fopenrx = ac->open(default_file_path, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(fopenrx);
        auto hFile = std::move(fopenrx).assume_value();

        BOOST_TEST_REQUIRE(ac->size_of(hFile).value() == file.size() + pos);

        auto readBuffer = std::make_unique<file_type>();
        TEST_RESULT_REQUIRE(ac->read(hFile, blob{ *readBuffer }, pos));

        BOOST_TEST(mismatch(file, blob_view{ *readBuffer }) == file.size());
    }
}

BOOST_AUTO_TEST_CASE(archive_query)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    constexpr std::uint64_t pos = detail::raw_archive::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    blob file{ *bigFile };

    utils::xoroshiro128plus dataGenerator{ 0 };
    dataGenerator.fill(file);

    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::create);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx = ac->open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        TEST_RESULT(ac->write(hFile, file, pos));

        BOOST_TEST_REQUIRE(ac->size_of(hFile).value() == file.size() + pos);
    }
    BOOST_TEST_PASSPOINT();
    {
        auto openrx = archive::open(fs, default_archive_path, cprov, default_user_prk, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();

        BOOST_TEST(!ac->query("somerandomfilename/asdflsdfmasfw/sadfaöjksdfn"));
        BOOST_TEST(!ac->query("somerandomfilename/asdflsdfmasfw/sadfaöjksdfn"));

        auto result = ac->query(default_file_path);
        BOOST_TEST_REQUIRE(result.has_value());

        BOOST_TEST(result.assume_value().size == file.size() + pos);
    }
}

BOOST_AUTO_TEST_CASE(sqlite_bridge_regression_1)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    using file_type = std::array<std::byte, 8192>;
    auto fileDataStorage = std::make_unique<file_type>();
    blob fileData{ *fileDataStorage };

    utils::xoroshiro128plus dataGenerator{ 0 };

    {
        auto openrx = archive::open(fs, "./xyz.vefs", cprov, blob_view{},
            file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();

        auto fopenrx = ac->open("blob-test-journal", file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fopenrx);
        auto f = std::move(fopenrx).assume_value();

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 0));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 8192));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 2*8192));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 3 * 8192));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 4*8192));

        TEST_RESULT_REQUIRE(ac->sync(f));
        f = nullptr;

        TEST_RESULT_REQUIRE(ac->erase("blob-test-journal"));


        fopenrx = ac->open("blob-test-journal", file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fopenrx);
        f = std::move(fopenrx).assume_value();

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 0));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 8192));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 2 * 8192));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 3 * 8192));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 4 * 8192));


        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 32772));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData.slice(0, 4), 40964));
        TEST_RESULT_REQUIRE(ac->write(f, fileData.slice(4, 4), 40968));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 40972));


        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData.slice(0, 4), 49164));

        TEST_RESULT_REQUIRE(ac->sync(f));
        f = nullptr;

        TEST_RESULT_REQUIRE(ac->erase("blob-test-journal"));
    }

}

BOOST_AUTO_TEST_SUITE_END()
