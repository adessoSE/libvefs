#include <boost/throw_exception.hpp>
#include "boost-unit-test.hpp"

#include <openssl/err.h>

#if defined(BOOST_COMP_MSVC_AVAILABLE)
#pragma warning(push, 3)
#pragma warning(disable : 4702) // unreachable code
#endif

#include <gmock/gmock.h>

#if defined(BOOST_COMP_MSVC_AVAILABLE)
#pragma warning(pop)
#endif

#include "vefs/crypto/provider.hpp"

#if defined BOOST_COMP_GNUC_AVAILABLE
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#endif

using namespace boost::unit_test;

class HookupListner : public ::testing::EmptyTestEventListener
{
public:
    void OnTestPartResult(const ::testing::TestPartResult &result)
    {
        boost::unit_test::unit_test_log
                << boost::unit_test::log::begin(result.file_name(),
                                                result.line_number())
                << boost::unit_test::log_all_errors << result.summary()
                << boost::unit_test::log::end();
        boost::unit_test::framework::assertion_result(
                result.passed() ? boost::unit_test::AR_PASSED
                                : boost::unit_test::AR_FAILED);
    }
};
// BoringSSL manually allocates TLS storage on first use
// which yields false positive memory leaks
// therefore we force the initialization of these structures to happen
// before the leak detector takes the baseline snapshot
struct BoringSslTlsFixture
{
    BoringSslTlsFixture()
    {
        ERR_clear_error();
    }
};
BoringSslTlsFixture BoringSslTlsInitializer;

// we can't use the macros to specify the module name, because the
// defining header is already included in the precompiled header...
bool init_unit_test()
{
    auto &suite{boost::unit_test::framework::master_test_suite()};
    ::testing::InitGoogleMock(&suite.argc, suite.argv);

    // hook up the gmock and boost test
    auto &listeners{::testing::UnitTest::GetInstance()->listeners()};
    delete listeners.Release(listeners.default_result_printer());
    listeners.Append(new HookupListner);

    framework::master_test_suite().p_name.value = "vefs test suite";
    return true;
}

// this way we don't have to care about whether Boost.Test was compiled with
// BOOST_TEST_ALTERNATIVE_INIT_API defined or not.
test_suite *init_unit_test_suite(int /*argc*/, char * /*argv*/[])
{
    if (!init_unit_test())
    {
        BOOST_THROW_EXCEPTION(framework::setup_error("init_unit_test failed."));
    }
    return nullptr;
}
