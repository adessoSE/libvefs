
// Copyright 2017-2023 Henrik Gaßmann
//
// Distributed under the Boost Software License, Version 1.0.
//         (See accompanying file LICENSE or copy at
//           https://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <dplx/predef/version_number.h>
#include <dplx/predef/workaround.h>

#include <vefs/config.hpp>
// NOLINTBEGIN(cppcoreguidelines-macro-usage)

// these macros are very similar to those in <dplx/predef/other/workaround.h>
// but offer library specific configuration knobs

// guard for bugs which have been resolved with a known (compiler) version
#define VEFS_WORKAROUND(symbol, comp, major, minor, patch)                \
    DPLX_XDEF_WORKAROUND(VEFS_DISABLE_WORKAROUNDS, symbol, comp, major,   \
                         minor, patch)

// guard for bugs which have _not_ been resolved known (compiler) version
// i.e. we need to periodically test whether they have been resolved
// after which we can move them in the upper category
#define VEFS_WORKAROUND_TESTED_AT(symbol, major, minor, patch)            \
    DPLX_XDEF_WORKAROUND_TESTED_AT(VEFS_DISABLE_WORKAROUNDS,              \
                                   VEFS_FLAG_OUTDATED_WORKAROUNDS,        \
                                   symbol, major, minor, patch)

////////////////////////////////////////////////////////////////////////////////

// NOLINTEND(cppcoreguidelines-macro-usage)
