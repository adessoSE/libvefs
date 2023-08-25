set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER clang-15)
set(CMAKE_CXX_COMPILER clang++-15)

set(CMAKE_C_STANDARD 17 CACHE STRING "default C standard target")
set(CMAKE_CXX_STANDARD 20 CACHE STRING "default C++ standard target")

set(CMAKE_CXX_FLAGS_INIT "-fsized-deallocation")
