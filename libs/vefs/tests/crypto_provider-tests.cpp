#include "boost-unit-test.hpp"
#include "../src/crypto_provider_boringssl.cpp"
#include <vefs\archive_fwd.hpp>
#include <vefs/utils/secure_allocator.hpp>
#include <vefs/utils/secure_array.hpp>
#include "../src/crypto_provider_debug.cpp"
#include <vefs/blob.hpp>
#include <array>

BOOST_AUTO_TEST_SUITE(crypto_provider_tests)

BOOST_AUTO_TEST_CASE(random_call)
{
    vefs::crypto::crypto_provider *test_subject =
        &vefs::crypto::detail::boringssl_aes_256_gcm_provider();
    std::byte randomState =  std::byte(0x00);
    auto random_bytes = vefs::rw_dynblob(&randomState, 1);
    
    test_subject->random_bytes(random_bytes);
    //#todo For ar real test we need mocking and mockable code
    BOOST_TEST(random_bytes.data());
}

BOOST_AUTO_TEST_CASE(boringssl_encrypts_end_decrypts_plaintext_to_same_value)
{
    vefs::crypto::crypto_provider *test_subject =
        &vefs::crypto::detail::boringssl_aes_256_gcm_provider();

    std::array<std::byte,44> key;
    std::array<std::byte,16> mac;
    vefs::utils::secure_vector<std::byte> msg{5, std::byte{}};
    
    for (int i = 0; i < 44; ++i)
    {
        key[i] = std::byte(0xbb);
    }
    for (int i = 0; i < 16; ++i)
    {
        mac[i] = std::byte(0xcc);
    }
    for (int i = 0; i < 5; ++i)
    {
        msg[i] = std::byte(0xaa);
    }
    auto key_span = vefs::span(key);
    auto msg_span = vefs::span(msg);
    auto mac_span = vefs::span(mac);
    
    test_subject->box_seal(msg_span, mac_span, key_span, msg_span);
    test_subject->box_open(msg_span, key_span, msg_span, mac_span);

    int plain_byte = 0xaa;
  
    BOOST_TEST(int(msg[0]) == plain_byte);
    BOOST_TEST(int(msg[1]) == plain_byte);
    BOOST_TEST(int(msg[2]) == plain_byte);
    BOOST_TEST(int(msg[3]) == plain_byte);
    BOOST_TEST(int(msg[4]) == plain_byte);  
}

BOOST_AUTO_TEST_CASE(boringssl_decrypts_returns_error_if_mac_is_18_bytes_long)
{
    vefs::crypto::crypto_provider *test_subject =
        &vefs::crypto::detail::boringssl_aes_256_gcm_provider();

    std::array<std::byte, 44> key;
    std::array<std::byte, 18> mac;
    vefs::utils::secure_vector<std::byte> msg{5, std::byte{}};

    for (int i = 0; i < 44; ++i)
    {
        key[i] = std::byte(0xbb);
    }
    for (int i = 0; i < 16; ++i)
    {
        mac[i] = std::byte(0xcc);
    }
    for (int i = 0; i < 5; ++i)
    {
        msg[i] = std::byte(0xaa);
    }
    auto key_span = vefs::span(key);
    auto msg_span = vefs::span(msg);
    auto mac_span = vefs::span(mac);

    test_subject->box_seal(msg_span, mac_span, key_span, msg_span);
    auto result = test_subject->box_open(msg_span, key_span, msg_span, mac_span);

    BOOST_TEST(result.has_error());
    BOOST_TEST(result.error().code()==5);
}

BOOST_AUTO_TEST_CASE(ct_compare_compares_two_equal_spans_return_true)
{
    std::array<std::byte, 5> key;
    std::array<std::byte, 5> mac;

    for (int i = 0; i < 5; ++i)
    {
        mac[i] = std::byte(0xcc);
        key[i] = std::byte(0xcc);
    }

    vefs::crypto::crypto_provider *test_subject =
        &vefs::crypto::detail::boringssl_aes_256_gcm_provider();

    auto result = test_subject->ct_compare(vefs::span(key), vefs::span(mac));

    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value()==0);
}

BOOST_AUTO_TEST_CASE(ct_compare_compares_second_larger_returns_1)
{
    std::array<std::byte, 5> first_to_compare;
    std::array<std::byte, 5> second_to_compare;

    for (int i = 0; i < 5; ++i)
    {
        second_to_compare[i] = std::byte(0xcc);
        first_to_compare[i] = std::byte(0xcd);
    }

    vefs::crypto::crypto_provider *test_subject =
        &vefs::crypto::detail::boringssl_aes_256_gcm_provider();

    auto result = test_subject->ct_compare(vefs::span(first_to_compare), vefs::span(second_to_compare));

    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value() == 1);
}

BOOST_AUTO_TEST_CASE(ct_compare_compares_second_larger_returns_minus_1)
{
    std::array<std::byte, 5> first_to_compare;
    std::array<std::byte, 5> second_to_compare;

    for (int i = 0; i < 5; ++i)
    {
        second_to_compare[i] = std::byte(0xcc);
        first_to_compare[i] = std::byte(0xca);
    }

    vefs::crypto::crypto_provider *test_subject =
        &vefs::crypto::detail::boringssl_aes_256_gcm_provider();

    auto result = test_subject->ct_compare(vefs::span(first_to_compare), vefs::span(second_to_compare));

    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value() == -1);
}

BOOST_AUTO_TEST_CASE(ct_comparing_two_different_size_arrays_gives_error)
{
    std::array<std::byte, 5> first_to_compare;
    std::array<std::byte, 3> second_with_smaller_size;

    for (int i = 0; i < 5; ++i)
    {
        first_to_compare[i] = std::byte(0xca);
    }
    for (int i = 0; i < 3; ++i)
    {
        second_with_smaller_size[i] = std::byte(0xca);
    }

    vefs::crypto::crypto_provider *test_subject =
        &vefs::crypto::detail::boringssl_aes_256_gcm_provider();

    auto result =
        test_subject->ct_compare(vefs::span(first_to_compare), vefs::span(second_with_smaller_size));

    BOOST_TEST(result.error().code()==2);
    BOOST_TEST(!result.has_value());
}

BOOST_AUTO_TEST_CASE(ct_comparing_two_zero_size_array_gives_error)
{
    std::array<std::byte, 0> first_to_compare;
    std::array<std::byte, 0> second_with_smaller_size;

    vefs::crypto::crypto_provider *test_subject =
        &vefs::crypto::detail::boringssl_aes_256_gcm_provider();

    auto result = test_subject->ct_compare(vefs::span(first_to_compare),
                                           vefs::span(second_with_smaller_size));

    BOOST_TEST(result.has_error());
    BOOST_TEST(result.error().code() == 2);
    BOOST_TEST(!result.has_value());
}



BOOST_AUTO_TEST_SUITE_END()
