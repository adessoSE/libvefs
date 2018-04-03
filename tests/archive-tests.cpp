#include <vefs/archive.hpp>
#include "boost-unit-test.hpp"

#include <vefs/detail/raw_archive.hpp>
#include <vefs/crypto/provider.hpp>
#include <vefs/utils/random.hpp>
#include "memfs.hpp"

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

    archive ac{ fs, default_archive_path, cprov, default_user_prk, archive::create };
}

BOOST_AUTO_TEST_CASE(archive_create_reopen)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::debug_crypto_provider();

    {
        archive ac{ fs, default_archive_path, cprov, default_user_prk, archive::create };
    }
    {
        archive ac{ fs, default_archive_path, cprov, default_user_prk };
    }
}

BOOST_AUTO_TEST_CASE(archive_create_file)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::debug_crypto_provider();

    {
        archive ac{ fs, default_archive_path, cprov, default_user_prk, archive::create };
        ac.open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
    }
    {
        archive ac{ fs, default_archive_path, cprov, default_user_prk };
        ac.open(default_file_path, file_open_mode::read);
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
        archive ac{ fs, default_archive_path, cprov, default_user_prk, archive::create };
        auto id = ac.open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
        ac.write(id, file, pos);
    }
    {
        archive ac{ fs, default_archive_path, cprov, default_user_prk };
        auto id = ac.open(default_file_path, file_open_mode::read);
        auto readBuffer = std::make_unique<file_type>();
        ac.read(id, blob{ *readBuffer }, pos);

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
        archive ac{ fs, default_archive_path, cprov, default_user_prk, archive::create };
        auto id = ac.open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
        ac.write(id, file, pos);
    }
    {
        archive ac{ fs, default_archive_path, cprov, default_user_prk };
        auto id = ac.open(default_file_path, file_open_mode::readwrite);
        ac.resize(id, 2 * detail::raw_archive::sector_payload_size);
    }
    {
        archive ac{ fs, default_archive_path, cprov, default_user_prk };
        auto id = ac.open(default_file_path, file_open_mode::readwrite);
        ac.resize(id, 0);
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
        archive ac{ fs, default_archive_path, cprov, default_user_prk, archive::create };
        auto id = ac.open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
        ac.write(id, file, pos);
    }
    {
        archive ac{ fs, default_archive_path, cprov, default_user_prk };
        ac.erase(default_file_path);
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
        archive ac{ fs, default_archive_path, cprov, blob_view{}, archive::create };
        auto id = ac.open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
        ac.write(id, file, pos);
        BOOST_TEST_REQUIRE(ac.size_of(id) == file.size() + pos);
    }
    {
        archive ac{ fs, default_archive_path, cprov, blob_view{} };
        auto id = ac.open(default_file_path, file_open_mode::read);
        BOOST_TEST_REQUIRE(ac.size_of(id) == file.size() + pos);

        auto readBuffer = std::make_unique<file_type>();
        ac.read(id, blob{ *readBuffer }, pos);

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
        archive ac{ fs, default_archive_path, cprov, blob_view{}, archive::create };
        auto id = ac.open(default_file_path, file_open_mode::readwrite | file_open_mode::create);
        ac.write(id, file, pos);
        BOOST_TEST_REQUIRE(ac.size_of(id) == file.size() + pos);
    }
    BOOST_TEST_PASSPOINT();
    {
        archive ac{ fs, default_archive_path, cprov, blob_view{} };

        BOOST_TEST(!ac.query("somerandomfilename/asdflsdfmasfw/sadfaöjksdfn"));
        BOOST_TEST(!ac.query("somerandomfilename/asdflsdfmasfw/sadfaöjksdfn"));

        auto result = ac.query(default_file_path);
        BOOST_TEST_REQUIRE(result.has_value());

        BOOST_TEST(result->size == file.size() + pos);
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
        std::shared_ptr<archive> ac = std::make_shared<archive>(fs, "./xyz.vefs",
            cprov, blob_view{}, archive::create);

        auto f = ac->open("blob-test-journal", file_open_mode::readwrite | file_open_mode::create);

        dataGenerator.fill(fileData);
        ac->write(f, fileData, 0);

        dataGenerator.fill(fileData);
        ac->write(f, fileData, 8192);

        dataGenerator.fill(fileData);
        ac->write(f, fileData, 2*8192);

        dataGenerator.fill(fileData);
        ac->write(f, fileData, 3 * 8192);

        dataGenerator.fill(fileData);
        ac->write(f, fileData, 4*8192);

        ac->sync(f);
        f = nullptr;

        ac->erase("blob-test-journal");


        f = ac->open("blob-test-journal", file_open_mode::readwrite | file_open_mode::create);

        dataGenerator.fill(fileData);
        ac->write(f, fileData, 0);

        dataGenerator.fill(fileData);
        ac->write(f, fileData, 8192);

        dataGenerator.fill(fileData);
        ac->write(f, fileData, 2 * 8192);

        dataGenerator.fill(fileData);
        ac->write(f, fileData, 3 * 8192);

        dataGenerator.fill(fileData);
        ac->write(f, fileData, 4 * 8192);


        dataGenerator.fill(fileData);
        ac->write(f, fileData, 32772);

        dataGenerator.fill(fileData);
        ac->write(f, fileData.slice(0, 4), 40964);
        ac->write(f, fileData.slice(4, 4), 40968);

        dataGenerator.fill(fileData);
        ac->write(f, fileData, 40972);


        dataGenerator.fill(fileData);
        ac->write(f, fileData.slice(0, 4), 49164);

        ac->sync(f);
        f = nullptr;

        ac->erase("blob-test-journal");
    }

}

BOOST_AUTO_TEST_SUITE_END()
