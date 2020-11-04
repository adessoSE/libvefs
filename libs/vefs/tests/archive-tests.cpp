#include <vefs/archive.hpp>
#include <vefs/utils/random.hpp>

#include "../src/detail/archive_file_id.hpp"
#include "../src/detail/sector_device.hpp"

#include "test-utils.hpp"

using namespace std::string_view_literals;

namespace
{
    constexpr std::array<std::byte, 32> default_user_prk{};
    static_assert(default_user_prk.size() == 32);

    constexpr auto default_archive_path = "./test-archive.vefs"sv;
    constexpr auto default_file_path = "diupdope"sv;
} // namespace

BOOST_AUTO_TEST_SUITE(vefs_archive_tests)

BOOST_AUTO_TEST_CASE(archive_create)
{
    using namespace vefs;

    auto cprov = test::only_mac_crypto_provider();
    auto archiveFileHandle = vefs::llfio::mapped_temp_inode().value();

    auto openrx = archive::open(std::move(archiveFileHandle), cprov,
                                default_user_prk, true);
    TEST_RESULT_REQUIRE(openrx);
    TEST_RESULT(openrx.assume_value()->commit());
}

BOOST_AUTO_TEST_CASE(archive_create_reopen)
{
    using namespace vefs;

    auto cprov = vefs::test::only_mac_crypto_provider();
    auto archiveFileHandle = vefs::llfio::mapped_temp_inode().value();

    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, true);
        TEST_RESULT_REQUIRE(openrx);
        TEST_RESULT_REQUIRE(openrx.assume_value()->commit());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, false);
        TEST_RESULT(openrx);
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto validaterx =
            archive::validate(std::move(cloned), cprov, default_user_prk);
        TEST_RESULT(validaterx);
    }
}

BOOST_AUTO_TEST_CASE(archive_no_parallel_open)
{
    using namespace vefs;

    auto cprov = vefs::test::only_mac_crypto_provider();
    auto archiveFileHandle = vefs::llfio::mapped_temp_inode().value();

    auto cloned = archiveFileHandle.reopen(0).value();
    auto openrx =
        archive::open(std::move(cloned), cprov, default_user_prk, true);
    TEST_RESULT_REQUIRE(openrx);
    TEST_RESULT_REQUIRE(openrx.assume_value()->commit());

    cloned = archiveFileHandle.reopen(0).value();
    auto reopenrx =
        archive::open(std::move(cloned), cprov, default_user_prk, false);
    BOOST_TEST(reopenrx.error() == errc::still_in_use);
}

BOOST_AUTO_TEST_CASE(archive_create_file)
{
    using namespace vefs;

    auto archiveFileHandle = vefs::llfio::mapped_temp_inode().value();
    auto cprov = vefs::test::only_mac_crypto_provider();

    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, true);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fopenrx = ac->open(default_file_path, file_open_mode::readwrite |
                                                       file_open_mode::create);
        TEST_RESULT_REQUIRE(fopenrx);
        TEST_RESULT_REQUIRE(ac->commit(fopenrx.assume_value()));
        TEST_RESULT_REQUIRE(ac->commit());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, false);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fopenrx = ac->open(default_file_path, file_open_mode::read);
        TEST_RESULT_REQUIRE(fopenrx);
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto validaterx =
            archive::validate(std::move(cloned), cprov, default_user_prk);
        TEST_RESULT(validaterx);
    }
}

BOOST_AUTO_TEST_CASE(archive_readwrite)
{
    using namespace vefs;

    auto archiveFileHandle = vefs::llfio::mapped_temp_inode().value();
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    constexpr std::uint64_t pos =
        detail::sector_device::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    span file{*bigFile};

    utils::xoroshiro128plus dataGenerator{0xC0DE'DEAD'BEEF'3ABA};
    dataGenerator.fill(file);

    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, true);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx =
            ac->open(default_file_path,
                     file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        TEST_RESULT(ac->write(hFile, file, pos));
        TEST_RESULT_REQUIRE(ac->commit(hFile));
        TEST_RESULT_REQUIRE(ac->commit());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, false);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx = ac->open(default_file_path, file_open_mode::read);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        auto readBuffer = std::make_unique<file_type>();
        TEST_RESULT(ac->read(hFile, span{*readBuffer}, pos));

        BOOST_TEST(mismatch_distance(file, span{*readBuffer}) == file.size());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto validaterx =
            archive::validate(std::move(cloned), cprov, default_user_prk);
        TEST_RESULT(validaterx);
    }
}

BOOST_AUTO_TEST_CASE(archive_file_shrink)
{
    using namespace vefs;

    auto archiveFileHandle = vefs::llfio::mapped_temp_inode().value();
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    constexpr std::uint64_t pos =
        detail::sector_device::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    span file{*bigFile};

    utils::xoroshiro128plus dataGenerator{0};
    dataGenerator.fill(file);

    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, true);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx =
            ac->open(default_file_path,
                     file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        TEST_RESULT(ac->write(hFile, file, pos));
        TEST_RESULT_REQUIRE(ac->commit(hFile));
        TEST_RESULT_REQUIRE(ac->commit());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, false);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fopenrx = ac->open(default_file_path, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(fopenrx);
        auto hFile = std::move(fopenrx).assume_value();

        TEST_RESULT(ac->truncate(
            hFile, 2 * detail::sector_device::sector_payload_size));

        TEST_RESULT_REQUIRE(ac->commit(hFile));
        TEST_RESULT_REQUIRE(ac->commit());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, false);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fopenrx = ac->open(default_file_path, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(fopenrx);
        auto hFile = std::move(fopenrx).assume_value();

        TEST_RESULT(ac->truncate(hFile, 0));

        TEST_RESULT_REQUIRE(ac->commit(hFile));
        TEST_RESULT_REQUIRE(ac->commit());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto validaterx =
            archive::validate(std::move(cloned), cprov, default_user_prk);
        TEST_RESULT(validaterx);
    }
}

BOOST_AUTO_TEST_CASE(archive_file_erase)
{
    using namespace vefs;

    auto archiveFileHandle = vefs::llfio::mapped_temp_inode().value();
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    constexpr std::uint64_t pos =
        detail::sector_device::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    span file{*bigFile};

    utils::xoroshiro128plus dataGenerator{0};
    dataGenerator.fill(file);

    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, true);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx =
            ac->open(default_file_path,
                     file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        TEST_RESULT(ac->write(hFile, file, pos));

        TEST_RESULT_REQUIRE(ac->commit(hFile));
        TEST_RESULT_REQUIRE(ac->commit());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, false);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();

        TEST_RESULT(ac->erase(default_file_path));

        TEST_RESULT_REQUIRE(ac->commit());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, false);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();

        auto qrx = ac->query(default_file_path);
        BOOST_REQUIRE(qrx.has_error() &&
                      qrx.assume_error() == archive_errc::no_such_file);
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto validaterx =
            archive::validate(std::move(cloned), cprov, default_user_prk);
        TEST_RESULT(validaterx);
    }
}

BOOST_AUTO_TEST_CASE(archive_empty_userprk)
{
    using namespace vefs;

    auto archiveFileHandle = vefs::llfio::mapped_temp_inode().value();
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    constexpr std::uint64_t pos =
        detail::sector_device::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    span file{*bigFile};

    utils::xoroshiro128plus dataGenerator{0};
    dataGenerator.fill(file);

    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, true);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx =
            ac->open(default_file_path,
                     file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        TEST_RESULT(ac->write(hFile, file, pos));

        BOOST_TEST_REQUIRE(ac->maximum_extent_of(hFile).value() ==
                           file.size() + pos);

        TEST_RESULT_REQUIRE(ac->commit(hFile));
        TEST_RESULT_REQUIRE(ac->commit());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, false);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fopenrx = ac->open(default_file_path, file_open_mode::readwrite);
        TEST_RESULT_REQUIRE(fopenrx);
        auto hFile = std::move(fopenrx).assume_value();

        BOOST_TEST_REQUIRE(ac->maximum_extent_of(hFile).value() ==
                           file.size() + pos);

        auto readBuffer = std::make_unique<file_type>();
        TEST_RESULT_REQUIRE(ac->read(hFile, span{*readBuffer}, pos));

        BOOST_TEST(mismatch_distance(file, span{*readBuffer}) == file.size());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto validaterx =
            archive::validate(std::move(cloned), cprov, default_user_prk);
        TEST_RESULT(validaterx);
    }
}

BOOST_AUTO_TEST_CASE(archive_query)
{
    using namespace vefs;

    auto archiveFileHandle = vefs::llfio::mapped_temp_inode().value();
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    constexpr std::uint64_t pos =
        detail::sector_device::sector_payload_size * 2 - 1;
    using file_type = std::array<std::byte, (1 << 17) * 3 - 1>;
    auto bigFile = std::make_unique<file_type>();
    span file{*bigFile};

    utils::xoroshiro128plus dataGenerator{0};
    dataGenerator.fill(file);

    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, true);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();
        auto fileOpenRx =
            ac->open(default_file_path,
                     file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fileOpenRx);
        auto hFile = std::move(fileOpenRx).assume_value();

        TEST_RESULT(ac->write(hFile, file, pos));

        BOOST_TEST_REQUIRE(ac->maximum_extent_of(hFile).value() ==
                           file.size() + pos);

        TEST_RESULT_REQUIRE(ac->commit(hFile));
        TEST_RESULT_REQUIRE(ac->commit());
    }
    BOOST_TEST_PASSPOINT();
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, false);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();

        BOOST_TEST(!ac->query("somerandomfilename/asdflsdfmasfw/sadfaöjksdfn"));
        BOOST_TEST(!ac->query("somerandomfilename/asdflsdfmasfw/sadfaöjksdfn"));

        auto result = ac->query(default_file_path);
        BOOST_TEST_REQUIRE(result.has_value());

        BOOST_TEST(result.assume_value().size == file.size() + pos);
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto validaterx =
            archive::validate(std::move(cloned), cprov, default_user_prk);
        TEST_RESULT(validaterx);
    }
}

BOOST_AUTO_TEST_CASE(sqlite_bridge_regression_1)
{
    using namespace vefs;

    auto archiveFileHandle = vefs::llfio::mapped_temp_inode().value();
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    using file_type = std::array<std::byte, 8192>;
    auto fileDataStorage = std::make_unique<file_type>();
    span fileData{*fileDataStorage};

    utils::xoroshiro128plus dataGenerator{0};

    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, true);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();

        auto fopenrx =
            ac->open("blob-test-journal",
                     file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fopenrx);
        auto f = std::move(fopenrx).assume_value();

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

        TEST_RESULT_REQUIRE(ac->commit(f));
        f = nullptr;

        TEST_RESULT_REQUIRE(ac->erase("blob-test-journal"));

        fopenrx = ac->open("blob-test-journal",
                           file_open_mode::readwrite | file_open_mode::create);
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
        TEST_RESULT_REQUIRE(ac->write(f, fileData.subspan(0, 4), 40964));
        TEST_RESULT_REQUIRE(ac->write(f, fileData.subspan(4, 4), 40968));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData, 40972));

        dataGenerator.fill(fileData);
        TEST_RESULT_REQUIRE(ac->write(f, fileData.subspan(0, 4), 49164));

        TEST_RESULT_REQUIRE(ac->commit(f));
        f = nullptr;

        TEST_RESULT_REQUIRE(ac->erase("blob-test-journal"));

        TEST_RESULT_REQUIRE(ac->commit());
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto validaterx =
            archive::validate(std::move(cloned), cprov, default_user_prk);
        TEST_RESULT(validaterx);
    }
}

BOOST_AUTO_TEST_CASE(sqlite_bridge_regression_2)
{
    using namespace vefs;

    auto archiveFileHandle = vefs::llfio::mapped_temp_inode().value();
    auto cprov = crypto::boringssl_aes_256_gcm_crypto_provider();

    using file_type = std::array<std::byte, 0x1000>;
    auto fileDataStorage = std::make_unique<file_type>();
    span fileData{*fileDataStorage};
    vefs::fill_blob(fileData, std::byte{0x55});

    utils::xoroshiro128plus dataGenerator{0};

    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto openrx =
            archive::open(std::move(cloned), cprov, default_user_prk, true);
        TEST_RESULT_REQUIRE(openrx);
        auto ac = std::move(openrx).assume_value();

        auto fopenrx =
            ac->open("db",
                     file_open_mode::readwrite | file_open_mode::create);
        TEST_RESULT_REQUIRE(fopenrx);
        auto f = std::move(fopenrx).assume_value();

        TEST_RESULT_REQUIRE(ac->commit(f));

        TEST_RESULT_REQUIRE(ac->commit());

        TEST_RESULT_REQUIRE(ac->write(f, fileData, 0x00000000));

        for (int i = 0; i < 0xf6; ++i)
        {
            BOOST_TEST_CONTEXT(i);

            TEST_RESULT_REQUIRE(
                ac->write(f, fileData, 0x0000000ull + i * 0x1000u));
        }

        TEST_RESULT_REQUIRE(ac->commit(f));

        TEST_RESULT_REQUIRE(ac->commit());

        TEST_RESULT_REQUIRE(ac->write(f, fileData, 0x0000b000));

        for (int j = 0; j < 98; ++j)
        {
            BOOST_TEST_CONTEXT(j);

            TEST_RESULT_REQUIRE(
                ac->write(f, fileData, 0x000f5000ull + j * 0x1000u));
        }

        TEST_RESULT_REQUIRE(ac->commit(f));

        TEST_RESULT_REQUIRE(ac->commit());

        TEST_RESULT_REQUIRE(ac->write(f, fileData, 0x000f4000));

        for (int j = 0; j < 111; ++j)
        {
            BOOST_TEST_CONTEXT(j);

            TEST_RESULT_REQUIRE(ac->write(f, fileData, 0x0010d000ull + j * 0x1000u));

        }

        TEST_RESULT_REQUIRE(ac->commit(f));

        TEST_RESULT_REQUIRE(ac->commit());



        f = nullptr;
    }
    {
        auto cloned = archiveFileHandle.reopen(0).value();
        auto validaterx =
            archive::validate(std::move(cloned), cprov, default_user_prk);
        TEST_RESULT(validaterx);
    }
}

BOOST_AUTO_TEST_CASE(archive_file_id_stringify)
{
    using namespace std::string_view_literals;
    using vefs::detail::file_id;

    file_id testId{vefs::utils::uuid{0xc7, 0xa5, 0x3d, 0x7a, 0xa4, 0xf0, 0x40,
                                     0x53, 0xa7, 0xa3, 0x35, 0xf3, 0x5c, 0xdf,
                                     0x53, 0x3d}};

    BOOST_TEST(fmt::format("{}", testId) ==
               "C7A53D7A-A4F0-4053-A7A3-35F35CDF533D"sv);
}

BOOST_AUTO_TEST_SUITE_END()
