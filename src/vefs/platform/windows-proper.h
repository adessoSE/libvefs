#pragma once

#if __has_include(<SDKDDKVer.h>)
// _WIN32_WINNT is defined per compiler flag
#include <SDKDDKVer.h>

#include <boost/predef.h>

#if defined BOOST_OS_WINDOWS_AVAILABLE

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#include <windows.h>

#endif

#else

#include <boost/predef.h>

#endif
