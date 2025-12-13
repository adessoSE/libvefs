set(_lxVCPKG_TARGET "osx")
include("${CMAKE_CURRENT_LIST_DIR}/_vcpkg.cmake")

execute_process(COMMAND brew --prefix llvm@19 OUTPUT_VARIABLE BREW_LLVM_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
set(CMAKE_OSX_DEPLOYMENT_TARGET 16.4 CACHE STRING "OSX deployment target")

set(CMAKE_C_COMPILER "${BREW_LLVM_PATH}/bin/clang")
set(CMAKE_CXX_COMPILER "${BREW_LLVM_PATH}/bin/clang++")
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "OSX target architectures")

include("${CMAKE_CURRENT_LIST_DIR}/_base.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/_clang.cmake")
