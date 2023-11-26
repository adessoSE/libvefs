#include "vefs/detail/sector_device.hpp"
#include "boost-unit-test.hpp"
#include "mocks.hpp"
#include "test-utils.hpp"

#include <vefs/span.hpp>
#include <vefs/utils/secure_array.hpp>

struct sector_device_test_fixture
{
    vefs::llfio::file_handle testFile;
    std::unique_ptr<vefs::detail::sector_device> testSubject;
    std::array<std::byte, 32> default_user_prk{};
    crypto_provider_mock cryptoProviderMock;

    sector_device_test_fixture()
        : testFile(vefs::llfio::temp_inode().value())

    {
        EXPECT_CALL(cryptoProviderMock, generate_session_salt())
                .WillRepeatedly(
                        testing::Return(vefs::utils::secure_byte_array<16>{}));
        EXPECT_CALL(cryptoProviderMock, random_bytes(testing::_))
                .WillRepeatedly([](vefs::rw_dynblob out) {
                    vefs::fill_blob(out, static_cast<std::byte>(0x11));
                    return vefs::outcome::success();
                });
        EXPECT_CALL(cryptoProviderMock,
                    box_seal(testing::_, testing::_, testing::_, testing::_))
                .WillRepeatedly([](auto &&...) -> vefs::result<void> {
                    return vefs::outcome::success();
                });

        testSubject = vefs::detail::sector_device::create_new(
                              testFile.reopen().value(), &cryptoProviderMock,
                              default_user_prk)
                              .value()
                              .device;
    }
};

BOOST_FIXTURE_TEST_SUITE(sector_device_tests, sector_device_test_fixture)

BOOST_AUTO_TEST_CASE(
        open_creates_new_device_with_random_value_for_master_secret)
{
    BOOST_TEST(testSubject->master_secret_view()[0] == std::byte(0x11));
}

BOOST_AUTO_TEST_CASE(open_existing_sector_device_throws_error_for_empty_file)
{
    auto emptyFile = vefs::llfio::temp_inode().value();
    auto deviceRx = vefs::detail::sector_device::open_existing(
            emptyFile.reopen().value(), &cryptoProviderMock, default_user_prk);

    BOOST_TEST(deviceRx.has_error());
    BOOST_TEST(deviceRx.assume_error()
               == vefs::archive_errc::no_archive_header);
}

// the currently used gtest version cannot match a std::span
// BOOST_AUTO_TEST_CASE(write_sector_seals_sector)
//{
//    // given
//    file_crypto_ctx_mock fileCryptoCtx;
//
//    auto testFile = vefs::llfio::mapped_temp_inode().value();
//    std::array<std::byte, 32> default_user_prk{};
//    std::array<std::byte, 16> sessionSalt{};
//    std::byte mac_data[16];
//    std::byte ro_data[32736];
//    vefs::fill_blob(vefs::rw_blob<32736>(ro_data), std::byte(0x1a));
//    auto mac = vefs::rw_blob<16>(mac_data);
//    auto dataBlob = vefs::ro_blob<32736>(ro_data);
//    auto testSubject = vefs::detail::sector_device::create_new(
//                               testFile.reopen(0).value(),
//                               &cryptoProviderMock, default_user_prk) .value()
//                               .device;
//
//    EXPECT_CALL(fileCryptoCtx,
//                seal_sector(testing::_, testing::ElementsAreArray(mac),
//                            testing::Ref(cryptoProviderMock),
//                            testing::ElementsAreArray(sessionSalt),
//                            testing::ElementsAreArray(ro_data)))
//            .Times(1)
//            .WillRepeatedly(testing::Return(vefs::outcome::success()));
//
//    // when
//    auto masterSectorId = vefs::detail::sector_id{1};
//    auto result
//            = testSubject
//                      ->write_sector<vefs::detail::file_crypto_ctx_interface>(
//                              mac, fileCryptoCtx, masterSectorId, dataBlob);
//}

// BOOST_AUTO_TEST_CASE(open_existing)
//{
//    auto second_device = vefs::detail::sector_device::open(
//        testFile.reopen(0).value(), &cryptoProviderMock,
//        default_user_prk, false);
//
//    BOOST_TEST(second_device.has_error());
//    BOOST_TEST(second_device.assume_error() ==
//               vefs::archive_errc::invalid_proto);
//}

BOOST_AUTO_TEST_CASE(write_sector_does_not_work_for_master_sector)
{
    std::byte mac_data[16];
    std::byte ro_data[32'736];
    vefs::fill_blob(vefs::rw_blob<32'736>(ro_data), std::byte(0x1a));
    auto mac = vefs::rw_blob<16>(mac_data);
    auto data_bla = vefs::ro_blob<32'736>(ro_data);
    auto fileCryptoCtx = vefs::detail::file_crypto_ctx(
            vefs::detail::file_crypto_ctx::zero_init_t{});
    auto masterSectorId = vefs::detail::sector_id::master;

    auto result = testSubject->write_sector<>(mac, fileCryptoCtx,
                                              masterSectorId, data_bla);

    BOOST_TEST(result.has_error());
    BOOST_TEST(result.assume_error() == vefs::errc::invalid_argument);
}

BOOST_AUTO_TEST_CASE(
        write_sector_gives_invalid_errc_for_sector_ids_that_is_to_great)
{
    std::byte mac_data[16];
    std::byte ro_data[32'736];
    auto mac = vefs::rw_blob<16>(mac_data);
    auto fileCryptoCtx = vefs::detail::file_crypto_ctx(
            vefs::detail::file_crypto_ctx::zero_init_t{});
    constexpr auto sectorIdxLimit
            = std::numeric_limits<std::uint64_t>::max() / (1 << 15);
    auto sectorId = static_cast<vefs::detail::sector_id>(sectorIdxLimit + 1);

    auto result = testSubject->write_sector(mac, fileCryptoCtx, sectorId,
                                            vefs::ro_blob<32'736>(ro_data));

    BOOST_TEST(result.has_error());
    BOOST_TEST(result.assume_error() == vefs::errc::invalid_argument);
}

BOOST_AUTO_TEST_CASE(
        read_sector_gives_invalid_errc_for_sector_ids_that_is_to_great)
{
    std::byte mac_data[16];
    std::byte rw_data[32'736];
    auto mac = vefs::ro_blob<16>(mac_data);
    auto fileCryptoCtx = vefs::detail::file_crypto_ctx(
            vefs::detail::file_crypto_ctx::zero_init_t{});
    constexpr auto sectorIdxLimit
            = std::numeric_limits<std::uint64_t>::max() / (1 << 15);
    auto sectorId = static_cast<vefs::detail::sector_id>(sectorIdxLimit + 1);

    auto result = testSubject->read_sector(vefs::rw_blob<32'736>(rw_data),
                                           fileCryptoCtx, sectorId, mac);

    BOOST_TEST(result.has_error());
    BOOST_TEST(result.assume_error() == vefs::errc::invalid_argument);
}

BOOST_AUTO_TEST_CASE(read_sector_does_not_work_for_master_sector)
{
    std::byte mac_data[16];
    std::byte rw_data[32'736];
    vefs::fill_blob(vefs::rw_blob<32'736>(rw_data), std::byte(0x1a));
    auto mac = vefs::rw_blob<16>(mac_data);
    auto fileCryptoCtx = vefs::detail::file_crypto_ctx(
            vefs::detail::file_crypto_ctx::zero_init_t{});
    auto masterSectorId = vefs::detail::sector_id::master;

    auto result = testSubject->read_sector(vefs::rw_blob<32'736>(rw_data),
                                           fileCryptoCtx, masterSectorId, mac);

    BOOST_TEST(result.has_error());
    BOOST_TEST(result.assume_error() == vefs::errc::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
