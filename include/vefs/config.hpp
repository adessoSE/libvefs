
// Copyright 2017-2023 Henrik Gaßmann
//
// Distributed under the Boost Software License, Version 1.0.
//         (See accompanying file LICENSE or copy at
//           https://www.boost.org/LICENSE_1_0.txt)

#pragma once

// the configuration generated by cmake is fully optional
#if __has_include(<vefs/detail/config.hpp>)
#include <vefs/detail/config.hpp>
#endif

#if !defined(VEFS_DISABLE_WORKAROUNDS)
#define VEFS_DISABLE_WORKAROUNDS 0
#endif
#if !defined(VEFS_FLAG_OUTDATED_WORKAROUNDS)
#define VEFS_FLAG_OUTDATED_WORKAROUNDS 0
#endif
