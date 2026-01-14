vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ned14/status-code
    REF b337c2278941f9c999b72dd74f4da1f7b726b173
    SHA512 c6460110ebaf1e186755a062baa3eb65d9284b9979b4a2a271a0bf0c2f416707ba1e5e5db84e638c8bad357fbd019edb9e0719c68d410645b069f0d03ebf2dce
    HEAD_REF master
    PATCHES
)

# Because status-code's deployed files are header-only, the debug build is not necessary
set(VCPKG_BUILD_TYPE release)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTING=OFF
        -Dstatus-code_IS_DEPENDENCY=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Boost=ON
    MAYBE_UNUSED_VARIABLES
        CMAKE_DISABLE_FIND_PACKAGE_Boost
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/status-code)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib")

file(INSTALL "${CURRENT_PORT_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/Licence.txt")
