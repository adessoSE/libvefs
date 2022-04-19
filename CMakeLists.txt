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
cmake_minimum_required(VERSION 3.16)

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
project(vefs VERSION 0.5.0.2 LANGUAGES CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/tools/cmake/")


##########################################################################
# options

set(VEFS_LLFIO_TARGET "sl" CACHE STRING "The llfio target to link against, can be 'hl', 'sl' or 'dl'")
set_property(CACHE VEFS_LLFIO_TARGET PROPERTY STRINGS "hl;sl;dl")

if (BUILD_TESTING)
    enable_testing()
endif()


########################################################################
# cmake modules
include(CMakePackageConfigHelpers)
include(GnuInstallDirs)
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

find_package(concrete CONFIG REQUIRED)
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

########################################################################
# warning configuration
include(CompilerWarnings)

##########################################################################
# compiler adjustments
add_library(compiler_settings INTERFACE)
add_library(Vefs::compiler_settings ALIAS compiler_settings)

if (CMAKE_CXX_EXTENSIONS)
    set(CMAKE_CXX_EXTENSIONS OFF CACHE STRING "" FORCE)
endif()
target_compile_features(compiler_settings INTERFACE cxx_std_20)
if (MSVC)
    target_compile_options(compiler_settings INTERFACE
        /utf-8 # all sources are utf-8 encoded
        /Zc:__cplusplus # correctly define the __cplusplus macro
    )
endif()
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(compiler_settings INTERFACE
        -municode
    )
endif()

if(WIN32)
    # specify winapi version
    add_definitions(-D_WIN32_WINNT=0x0601)
    # force unicode WINAPI symbols
    add_definitions(-DUNICODE -D_UNICODE)
endif()

install(TARGETS compiler_settings EXPORT vefs-targets)


########################################################################
# configure output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")


##########################################################################
# the actual library project
if (BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(libs/vefs)
#add_subdirectory(libs/vefs-cli)
if (benchmark_FOUND)
    add_subdirectory(extras/benchmark)
endif()


##########################################################################
# cmake install
install(EXPORT vefs-targets
    NAMESPACE Vefs::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
    COMPONENT core
)

configure_package_config_file(tools/vefs-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/vefs-config.cmake"

    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vefs-config-version.cmake"
    COMPATIBILITY SameMinorVersion
)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/vefs-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/vefs-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
    COMPONENT core
)
