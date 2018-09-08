# Written in 2016 by Henrik Steffen Gaßmann <henrik@gassmann.onl>
#
# To the extent possible under law, the author(s) have dedicated all
# copyright and related and neighboring rights to this software to the
# public domain worldwide. This software is distributed without any warranty.
#
# You should have received a copy of the CC0 Public Domain Dedication
# along with this software. If not, see
#
#     http://creativecommons.org/publicdomain/zero/1.0/
#
########################################################################
# Tries to find the local botan installation.
#
# Once done the following variables will be defined:
#
#   botan_FOUND
#   botan_LIBRARIES
#   botan_INCLUDE_DIR [cache]
#   botan_LIBRARY_DEBUG [cache]
#   botan_LIBRARY_RELEASE [cache]
#
#
# Furthermore an imported "botan" target is created.
#

find_path(botan_INCLUDE_DIR botan/build.h)

if (NOT botan_INCLUDE_DIR STREQUAL botan_INCLUDE_DIR-NOTFOUND)
    get_filename_component(_botan_DIR "${botan_INCLUDE_DIR}" DIRECTORY)

    find_library(botan_LIBRARY_RELEASE botan HINTS "${_botan_DIR}" PATH_SUFFIXES lib NO_DEFAULT_PATH)
    find_library(botan_LIBRARY_DEBUG botan HINTS "${_botan_DIR}/debug" PATH_SUFFIXES lib NO_DEFAULT_PATH)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(botan
    REQUIRED_VARS botan_INCLUDE_DIR botan_LIBRARY_RELEASE
)

if (NOT (botan_LIBRARY_RELEASE STREQUAL botan_LIBRARY_RELEASE-NOTFOUND OR botan_INCLUDE_DIR STREQUAL botan_INCLUDE_DIR-NOTFOUND))
    # TODO: determnine whether botan is dynamic and add the so paths
    add_library(botan STATIC IMPORTED)

    set_target_properties(botan PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${botan_INCLUDE_DIR}"
        IMPORTED_LOCATION "${botan_LIBRARY_RELEASE}"
        IMPORTED_LOCATION_RELEASE "${botan_LIBRARY_RELEASE}"
    )

    if (NOT botan_LIBRARY_DEBUG STREQUAL botan_LIBRARY_DEBUG-NOTFOUND)
        set_target_properties(botan PROPERTIES
            IMPORTED_LOCATION_DEBUG "${botan_LIBRARY_DEBUG}"
        )
    endif()
endif()
