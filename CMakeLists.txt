# Written in 2017, 2019, 2022-2023 by Henrik Steffen Gaßmann <henrik@gassmann.onl>
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
cmake_minimum_required(VERSION 3.23...4.0)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/tools/cmake/")

########################################################################
# configure vcpkg from environment vars if possible
include(VcpkgDefaults)

if (BUILD_TESTING)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()
if (BUILD_CLI)
    list(APPEND VCPKG_MANIFEST_FEATURES "cli")
endif()

########################################################################
project(vefs
    VERSION 0.6.0.0
    LANGUAGES CXX
)

include(EnforceOutOfSourceBuilds)
include(CMakeDependentOption)
include(CMakePackageConfigHelpers)
include(FeatureSummary)
include(GNUInstallDirs)

include(SourceHelpers)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
if (BUILD_TESTING)
    enable_testing()
endif()


########################################################################
# options
option(BUILD_CLI "Build the CommandLine-Interface" OFF)
option(BUILD_TESTING "Build the unit tests" OFF)

option(VEFS_DISABLE_WORKAROUNDS "Disable all workarounds" OFF)
option(VEFS_FLAG_OUTDATED_WORKAROUNDS "Emit compiler errors for workarounds which are active, but haven't been validated for this version" OFF)

set(VEFS_LLFIO_TARGET "sl" CACHE STRING "The llfio target to link against, can be 'hl', 'sl' or 'dl'")
set_property(CACHE VEFS_LLFIO_TARGET PROPERTY STRINGS "hl;sl;dl")

########################################################################
# dependencies

find_package(base64 CONFIG REQUIRED)

find_package(fmt CONFIG REQUIRED)

find_package(unofficial-concurrentqueue CONFIG REQUIRED)

find_package(libb2 REQUIRED CONFIG)

find_package(libcuckoo CONFIG REQUIRED)

find_package(stduuid CONFIG REQUIRED)

find_package(OpenSSL CONFIG REQUIRED COMPONENTS Crypto)

if (BUILD_TESTING)
    set(_VEFS_REQUIRE_UNIT_TEST_FRAMEWORK unit_test_framework)
endif()
find_package(Boost 1.82 REQUIRED COMPONENTS
    ${_VEFS_REQUIRE_UNIT_TEST_FRAMEWORK}
)

find_package(status-code CONFIG REQUIRED)
find_package(outcome CONFIG REQUIRED)
find_package(llfio CONFIG REQUIRED)

# botan provider is currently not implemented
#find_package(botan 2.8.0)

find_package(concrete CONFIG REQUIRED)
find_package(deeppack CONFIG REQUIRED)
find_package(deeplog CONFIG REQUIRED)

if (BUILD_CLI)
    find_package(lyra REQUIRED)
endif()

if (BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
endif()

find_package(Catch2 CONFIG)
set_package_properties(Catch2 PROPERTIES
    TYPE OPTIONAL
    PURPOSE "Allows to build the test suite"
)
cmake_dependent_option(BUILD_TESTING "Build the documentation using sphinx" ON Catch2_FOUND OFF)

find_package(Sphinx)
set_package_properties(Sphinx PROPERTIES
    TYPE OPTIONAL
    PURPOSE "Allows to build the documentation"
)
cmake_dependent_option(BUILD_DOCS "Build the documentation using sphinx" ON Sphinx_FOUND OFF)

if (NOT (botan_FOUND OR OpenSSL_FOUND))
    message(WARNING "no crypto provider library (BoringSSL, botan) could be found.")
endif()


########################################################################
# warning configuration
include(CompilerWarnings)


########################################################################
# additional compiler options
set(CMAKE_CXX_EXTENSIONS OFF CACHE STRING "" FORCE)

add_library(compiler_settings INTERFACE)
add_library(Vefs::vefs_compiler_settings ALIAS compiler_settings)
set_target_properties(compiler_settings PROPERTIES
    EXPORT_NAME vefs_compiler_settings
)

target_compile_features(compiler_settings INTERFACE cxx_std_20)
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(compiler_settings INTERFACE
        /utf-8 # all sources are utf-8 encoded
        /Zc:__cplusplus # correctly define the __cplusplus macro
    )
endif()


########################################################################
# library
add_library(vefs)
add_library(Vefs::vefs ALIAS vefs)

target_link_libraries(vefs
    PUBLIC
        Vefs::vefs_compiler_settings

        Boost::boost
        Deeplex::deeppack
        Deeplex::deeplog
        libb2
        fmt::fmt
        llfio::${VEFS_LLFIO_TARGET}
        outcome::hl
        unofficial::concurrentqueue::concurrentqueue
        libcuckoo::libcuckoo
        stduuid
)

target_compile_definitions(vefs
    PRIVATE
        VEFS_DISABLE_WORKAROUNDS=$<BOOL:${VEFS_DISABLE_WORKAROUNDS}>
        VEFS_FLAG_OUTDATED_WORKAROUNDS=$<BOOL:${VEFS_FLAG_OUTDATED_WORKAROUNDS}>
)

target_include_directories(vefs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/src>
    $<INSTALL_INTERFACE:include>
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(vefs
        PRIVATE
            advapi32
    )
    target_compile_definitions(vefs
        PRIVATE
            # force unicode WINAPI symbols
            -DUNICODE -D_UNICODE
    )
endif()

if (botan_FOUND)
    target_link_libraries(vefs PUBLIC botan)
endif()
if (OpenSSL_FOUND)
    target_link_libraries(vefs PUBLIC OpenSSL::Crypto)
endif()


########################################################################
# library test project
if (BUILD_TESTING)
    add_executable(vefs-tests)

    target_link_libraries(vefs-tests PRIVATE
        Vefs::vefs

        Boost::unit_test_framework
        GTest::gmock
    )

    target_include_directories(vefs-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_utils
    )

    add_test(NAME vefs-tests COMMAND vefs-tests)
endif()

########################################################################
# source files
include(sources.cmake)
source_group(TREE src/vefs)
source_group(TREE include/vefs)


########################################################################
# cli project
if (BUILD_CLI)
    add_subdirectory(apps)
endif()

########################################################################
# docs
if (BUILD_DOCS)
    add_subdirectory(docs)
endif()

########################################################################
# cmake install
install(DIRECTORY include/vefs
    TYPE INCLUDE
    FILES_MATCHING
    REGEX ".*\\.h(pp)?"
)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated/src
    TYPE INCLUDE
    FILES_MATCHING
    REGEX ".*\\.h(pp)?"
)

install(TARGETS vefs compiler_settings EXPORT vefs-targets)
install(EXPORT vefs-targets
    NAMESPACE Vefs::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vefs"
)

export(EXPORT vefs-targets NAMESPACE Vefs::)
# note that you need to configure with -DCMAKE_EXPORT_PACKAGE_REGISTRY=ON
# for this to have any effect at all
export(PACKAGE vefs)

configure_package_config_file(tools/vefs-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/vefs-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vefs"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vefs-config-version.cmake"
    COMPATIBILITY SameMinorVersion
)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/vefs-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/vefs-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vefs"
)

########################################################################
feature_summary(WHAT PACKAGES_FOUND PACKAGES_NOT_FOUND)
