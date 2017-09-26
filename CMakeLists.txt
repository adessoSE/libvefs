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
cmake_minimum_required(VERSION 3.9)
project(node-vefs VERSION 0.0.0)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "in-source builds are not supported!")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

if (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(GCC_COMPATIBLE 1)
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(GXX_COMPATIBLE 1)
endif()


##########################################################################
# options


########################################################################
# cmake modules

# cotire
include(cotire)


##########################################################################
# library includes

# libsodium
find_package(sodium REQUIRED)

# protobuf + protoc
set(Protobuf_SRC_ROOT_FOLDER "$ENV{Protobuf_DIR}")
find_package(Protobuf QUIET)
if (NOT Protobuf_FOUND)
    # try again with config mode
    find_package(protobuf CONFIG REQUIRED)
endif()


##########################################################################
# compiler adjustments

# we require c++14 and some c++17 features
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (CMAKE_VERSION VERSION_LESS "3.10" AND MSVC)
    # workaround until cmake 3.10 gets available
    if (MSVC_VERSION LESS 1911)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std:c++latest")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std:c++17")
    endif()
endif()

# adjust compiler warning levels
if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS)
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
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4324")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4324")
endif()

if (GCC_COMPATIBLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-long-long -Wpedantic")
endif()
if (GXX_COMPATIBLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long -Wpedantic")
endif()

# additional compiler tweaks
if (MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /utf-8")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
endif()


##########################################################################
# the actual node addon project
add_subdirectory(src)
