# Written in 2017, 2019 by Henrik Steffen Gaßmann <henrik@gassmann.onl>
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
cmake_minimum_required(VERSION 3.20)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/tools/cmake/")

########################################################################
# configure vcpkg from environment vars if possible
include(VcpkgDefaults)

if (BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

########################################################################
project(vefs
    VERSION 0.5.0.3
    LANGUAGES CXX
)

include(EnforceOutOfSourceBuilds)
include(CMakeDependentOption)
include(CMakePackageConfigHelpers)
include(FeatureSummary)
include(GNUInstallDirs)

include(SourceHelpers)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)


########################################################################
# options

option(VEFS_DISABLE_WORKAROUNDS "Disables all code workarounds" OFF)
option(VEFS_FLAG_OUTDATED_WORKAROUNDS "Raises compiler errors for all workarounds which need to be reevaluated" OFF)

set(VEFS_LLFIO_TARGET "sl" CACHE STRING "The llfio target to link against, can be 'hl', 'sl' or 'dl'")
set_property(CACHE VEFS_LLFIO_TARGET PROPERTY STRINGS "hl;sl;dl")

########################################################################
# dependencies

find_package(Catch2 CONFIG)
cmake_dependent_option(BUILD_TESTING "Build the documentation using sphinx" ON Catch2_FOUND OFF)
set_package_properties(Catch2 PROPERTIES
    TYPE OPTIONAL
    PURPOSE "Allows to build the test suite"
)

find_package(Sphinx)
cmake_dependent_option(BUILD_DOCS "Build the documentation using sphinx" ON Sphinx_FOUND OFF)
set_package_properties(Sphinx PROPERTIES
    TYPE OPTIONAL
    PURPOSE "Allows to build the documentation"
)

find_package(outcome CONFIG REQUIRED)

find_package(llfio CONFIG REQUIRED)

find_package(unofficial-concurrentqueue CONFIG REQUIRED)

find_package(libb2 REQUIRED CONFIG)

find_package(libcuckoo CONFIG REQUIRED)

find_package(stduuid CONFIG REQUIRED)

find_package(OpenSSL REQUIRED)

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
        Boost::system
        Deeplex::deeppack
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

target_include_directories(vefs
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(vefs
        PRIVATE
            advapi32
    )
    target_compile_definitions(vefs
        PRIVATE
            # specify winapi version to windows 8.1
            -D_WIN32_WINNT=0x0603
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
    )

    add_test(NAME vefs-tests COMMAND vefs-tests)
endif()

########################################################################
# source files
include(sources.cmake)
source_group(TREE src/vefs)
source_group(TREE include/vefs)


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

install(TARGETS vefs compiler_settings EXPORT vefs-targets)
install(EXPORT vefs-targets
    NAMESPACE Vefs::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vefs"
)

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
