# Written in 2017 by Henrik Steffen Gaßmann <henrik@gassmann.onl>
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
cmake_minimum_required(VERSION 3.15)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "in-source builds are not supported!")
endif()

########################################################################
# configure vcpkg from environment vars if possible

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "")
endif()

if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET} AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}" CACHE STRING "")
endif()

if(NOT DEFINED VCPKG_FEATURE_FLAGS)
    set(VCPKG_FEATURE_FLAGS "registries" CACHE STRING "")
endif()
if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
    set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_SOURCE_DIR}/tools/triplets" CACHE STRING "")
endif()

option(BUILD_TESTING "Add test targets" OFF)
if (BUILD_TESTING)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

########################################################################
project(vefs VERSION 0.3.2 LANGUAGES CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/tools/cmake/")

if (CMAKE_C_COMPILER_ID STREQUAL "GNU"
    OR (CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_C_SIMULATE_ID STREQUAL "MSVC"))
    set(GCC_COMPATIBLE 1)
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
    set(GXX_COMPATIBLE 1)
endif()

if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
    set(MSVC_COMPATIBLE 1)
endif()


##########################################################################
# options


########################################################################
# cmake modules
include(src-helper)


##########################################################################
# library includes

find_package(outcome CONFIG REQUIRED)

find_package(llfio CONFIG REQUIRED)

find_package(unofficial-concurrentqueue CONFIG REQUIRED)

find_package(libb2 REQUIRED CONFIG)

find_package(libcuckoo CONFIG REQUIRED)

find_package(boringssl REQUIRED CONFIG)

if (BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
endif()

# botan provider is currently not implemented
#find_package(botan 2.8.0)

find_package(deeppack CONFIG REQUIRED)

if (BUILD_TESTING)
    set(_VEFS_REQUIRE_UNIT_TEST_FRAMEWORK unit_test_framework)
endif()
find_package(Boost 1.70 REQUIRED COMPONENTS
    system
    program_options
    ${_VEFS_REQUIRE_UNIT_TEST_FRAMEWORK}
)

find_package(benchmark)

# fmt library
find_package(fmt CONFIG REQUIRED)

if (NOT (botan_FOUND OR boringssl_FOUND))
    message(WARNING "no crypto provider library (BoringSSL, botan) could be found.")
endif()

##########################################################################
# compiler adjustments

# we require c++14 and some c++17 features
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# adjust compiler warning levels
if (MSVC_COMPATIBLE)
    if(${CMAKE_C_FLAGS} MATCHES "/W[0-4]")
        string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    else()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
    endif()
    if(${CMAKE_CXX_FLAGS} MATCHES "/W[0-4]")
        string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else()
        set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    endif()

    # disable "structure was padded due to alignment specifier" warning
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4324 /D_CRT_SECURE_NO_WARNINGS /D_SCL_SECURE_NO_WARNINGS")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4324 /D_CRT_SECURE_NO_WARNINGS /D_SCL_SECURE_NO_WARNINGS")
endif()

if (GCC_COMPATIBLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-long-long -Wpedantic")
endif()
if (GXX_COMPATIBLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long -Wpedantic -Wno-unknown-pragmas -Wno-deprecated-declarations")
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wno-c++98-compat -Wno-reserved-id-macro)
endif()

# additional compiler tweaks
if (MSVC_COMPATIBLE)
    # protobuf uses std::iterator within repeated_field.h
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /utf-8 /permissive-")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8 /permissive- /D_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING")
endif()

if(WIN32)
    # specify winapi version
    add_definitions(-D_WIN32_WINNT=0x0601)
    # force unicode WINAPI symbols
    add_definitions(-DUNICODE -D_UNICODE)
endif()
macro(add_mingw_unicode_flags)
    if(MINGW AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -municode")
    endif()
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -municode")
    endif()
endmacro()


########################################################################
# configure output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")


##########################################################################
# the actual library project
enable_testing()

add_subdirectory(libs/vefs)
#add_subdirectory(libs/vefs-cli)
if (benchmark_FOUND)
    add_subdirectory(extras/benchmark)
endif()


##########################################################################
# install targets
configure_file(tools/vefs-config.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/vefs-config.cmake"
    COPYONLY
)

install(EXPORT vefs-targets
    NAMESPACE Vefs::
    DESTINATION cmake
    COMPONENT core
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vefs-config-version.cmake"
    VERSION "${VEFS_VERSION}"
    COMPATIBILITY ExactVersion
)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/vefs-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/vefs-config-version.cmake"
    DESTINATION cmake
    COMPONENT core
)
