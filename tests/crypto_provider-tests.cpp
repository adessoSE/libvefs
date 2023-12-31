
#include "boost-unit-test.hpp"
#include "vefs/crypto/crypto_provider_boringssl.hpp"

#include <array>

#include <vefs/archive_fwd.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/secure_allocator.hpp>
#include <vefs/utils/secure_array.hpp>

#include "test-utils.hpp"

struct crypto_provider_test_fixture
{
    vefs::crypto::detail::boringssl_aes_256_gcm_provider provider_impl;
    vefs::crypto::crypto_provider *test_subject;

    crypto_provider_test_fixture()
        : provider_impl()
        , test_subject(&provider_impl)
    {
    }
};

BOOST_FIXTURE_TEST_SUITE(crypto_provider_tests, crypto_provider_test_fixture)

BOOST_AUTO_TEST_CASE(random_call)
{
    std::array<std::byte, 16> random_bytes{};
    TEST_RESULT(test_subject->random_bytes(std::span(random_bytes)));
}

BOOST_AUTO_TEST_CASE(boringssl_encrypts_end_decrypts_plaintext_to_same_value)
{
    std::array<std::byte, 44> key;
    std::array<std::byte, 16> mac;
    vefs::utils::secure_vector<std::byte> msg{5, std::byte{}};

    auto key_span = std::span(key);
    auto msg_span = std::span(msg);
    auto mac_span = std::span(mac);

    vefs::fill_blob(key_span, std::byte{0xbb});
    vefs::fill_blob(mac_span, std::byte{0xcc});
    vefs::fill_blob(msg_span, std::byte{0xaa});

    auto seal_result
            = test_subject->box_seal(msg_span, mac_span, key_span, msg_span);
    auto open_result
            = test_subject->box_open(msg_span, key_span, msg_span, mac_span);

    auto plain_byte = 0xaa;

    BOOST_TEST(!seal_result.has_error());
    BOOST_TEST(!open_result.has_error());
    BOOST_TEST(int(msg[0]) == plain_byte);
    BOOST_TEST(int(msg[1]) == plain_byte);
    BOOST_TEST(int(msg[2]) == plain_byte);
    BOOST_TEST(int(msg[3]) == plain_byte);
    BOOST_TEST(int(msg[4]) == plain_byte);
}

BOOST_AUTO_TEST_CASE(boringssl_decrypts_returns_error_if_mac_is_18_bytes_long)
{
    std::array<std::byte, 44> key;
    std::array<std::byte, 18> mac;
    vefs::utils::secure_vector<std::byte> msg{5, std::byte{}};

    auto key_span = std::span(key);
    auto msg_span = std::span(msg);
    auto mac_span = std::span(mac);

    vefs::fill_blob(key_span, std::byte{0xbb});
    vefs::fill_blob(mac_span, std::byte{0xcc});
    vefs::fill_blob(msg_span, std::byte{0xaa});

    auto seal_result
            = test_subject->box_seal(msg_span, mac_span, key_span, msg_span);
    auto open_result
            = test_subject->box_open(msg_span, key_span, msg_span, mac_span);

    BOOST_TEST(!seal_result.has_error());
    BOOST_TEST(open_result.has_error());
    BOOST_TEST(open_result.error().value() == 5);
}

BOOST_AUTO_TEST_CASE(ct_compare_compares_two_equal_spans_return_true)
{
    std::array<std::byte, 5> key;
    std::array<std::byte, 5> mac;

    auto key_span = std::span(key);
    auto mac_span = std::span(mac);

    vefs::fill_blob(mac_span, std::byte{0xcc});
    vefs::fill_blob(key_span, std::byte{0xcc});

    auto result = test_subject->ct_compare(key_span, mac_span);

    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value() == 0);
}

BOOST_AUTO_TEST_CASE(ct_compare_compares_second_smaller_returns_1)
{
    std::array<std::byte, 5> first_to_compare;
    std::array<std::byte, 5> second_to_compare;

    auto first_to_compare_span = std::span(first_to_compare);
    auto second_to_compare_span = std::span(second_to_compare);

    vefs::fill_blob(first_to_compare_span, std::byte{0xcd});
    vefs::fill_blob(second_to_compare_span, std::byte{0xcc});

    auto result = test_subject->ct_compare(first_to_compare_span,
                                           second_to_compare_span);

    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value() == 1);
}

BOOST_AUTO_TEST_CASE(ct_compare_compares_second_larger_returns_minus_1)
{
    std::array<std::byte, 5> first_to_compare;
    std::array<std::byte, 5> second_to_compare;

    auto first_to_compare_span = std::span(first_to_compare);
    auto second_to_compare_span = std::span(second_to_compare);

    vefs::fill_blob(first_to_compare_span, std::byte{0xca});
    vefs::fill_blob(second_to_compare_span, std::byte{0xcc});

    auto result = test_subject->ct_compare(first_to_compare_span,
                                           second_to_compare_span);

    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value() == -1);
}

BOOST_AUTO_TEST_CASE(ct_comparing_two_different_size_arrays_gives_error)
{
    std::array<std::byte, 5> first_to_compare;
    std::array<std::byte, 3> second_shorter;

    auto first_to_compare_span = std::span(first_to_compare);
    auto second_shorter_span = std::span(second_shorter);

    vefs::fill_blob(first_to_compare_span, std::byte{0xca});
    vefs::fill_blob(second_shorter_span, std::byte{0xca});

    auto result = test_subject->ct_compare(first_to_compare_span,
                                           second_shorter_span);

    BOOST_TEST(result.error() == vefs::errc::invalid_argument);
    BOOST_TEST(!result.has_value());
}

BOOST_AUTO_TEST_CASE(ct_comparing_two_zero_size_arrays_gives_error)
{
    std::array<std::byte, 0> first_to_compare;
    std::array<std::byte, 0> second_with_smaller_size;

    auto result = test_subject->ct_compare(std::span(first_to_compare),
                                           std::span(second_with_smaller_size));

    BOOST_TEST(result.has_error());
    BOOST_TEST(result.error() == vefs::errc::invalid_argument);
    BOOST_TEST(!result.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
