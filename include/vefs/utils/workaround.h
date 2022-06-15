#pragma once

#include <dplx/predef/workaround.h>

#define VEFS_WORKAROUND(symbol, comp, major, minor, patch)                     \
    DPLX_XDEF_WORKAROUND(VEFS_DISABLE_WORKAROUNDS, symbol, comp, major, minor, \
                         patch)

#define VEFS_WORKAROUND_TESTED_AT(symbol, major, minor, patch)                 \
    DPLX_XDEF_WORKAROUND_TESTED_AT(VEFS_DISABLE_WORKAROUNDS,                   \
                                   VEFS_FLAG_OUTDATED_WORKAROUNDS, symbol,     \
                                   major, minor, patch)
