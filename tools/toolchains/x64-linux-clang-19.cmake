set(CMAKE_C_COMPILER clang-19)
set(CMAKE_CXX_COMPILER clang++-19)

set(_lxVCPKG_TARGET "linux")
include("${CMAKE_CURRENT_LIST_DIR}/_vcpkg.cmake")

if (NOT DEFINED CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR AMD64)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/_base.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/_clang.cmake")
