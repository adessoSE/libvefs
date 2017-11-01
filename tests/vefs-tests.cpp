#include "boost-unit-test.hpp"
#include <boost/throw_exception.hpp>

#include <vefs/crypto/provider.hpp>

using namespace boost::unit_test;


// we can't use the macros to specify the module name, because the
// defining header is already included in the precompiled header...
bool init_unit_test()
{
    vefs::crypto::detail::enable_debug_provider();

    framework::master_test_suite().p_name.value = "vefs test suite";
    return true;
}

// this way we don't have to care about whether Boost.Test was compiled with
// BOOST_TEST_ALTERNATIVE_INIT_API defined or not.
test_suite * init_unit_test_suite(int /*argc*/, char* /*argv*/[])
{
    if (!init_unit_test())
    {
        BOOST_THROW_EXCEPTION(framework::setup_error("init_unit_test failed."));
    }
    return nullptr;
}
