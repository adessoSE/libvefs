set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/x64-linux-clang-16.cmake")

if (PORT STREQUAL "boringssl")
    set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_CXX_STANDARD=17")
endif()
