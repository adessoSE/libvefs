#pragma once

#include <boost/predef.h>

#if defined BOOST_COMP_GNUC_AVAILABLE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push, 2)
#pragma warning(disable : 4702)
#endif

#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/parameterized_test.hpp>
#include <boost/test/unit_test.hpp>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif

#if defined BOOST_COMP_GNUC_AVAILABLE
#pragma GCC diagnostic pop
#endif
