#include <vefs/archive.hpp>
#include "boost-unit-test.hpp"

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

BOOST_AUTO_TEST_SUITE_END()
