set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER gcc-12)
set(CMAKE_CXX_COMPILER g++-12)
set(CMAKE_OSX_DEPLOYMENT_TARGET 14.2 CACHE STRING "OSX deployment target")
set(CMAKE_OSX_ARCHITECTURES x86_64 CACHE STRING "OSX target architectures")

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

# the new linker currently segfaults with gcc (I believe this is fixed with gcc 14)
set(CMAKE_CXX_FLAGS_INIT "-Wl,-ld_classic")
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
