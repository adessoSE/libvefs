set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

set(_lxVCPKG_TARGET "linux")
include("${CMAKE_CURRENT_LIST_DIR}/_vcpkg.cmake")

if (NOT DEFINED CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR AMD64)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/_base.cmake")
