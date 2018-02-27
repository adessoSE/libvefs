protobuf_generate_cpp(_FILEPROTO_SRCS _FILEPROTO_HDRS fileformat.proto)

set(_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")

DEF_SOURCE_GROUP(vefs UTILS "utils"
    SOURCES
        # the precompiled.cpp _must_ be the first source file
        # due to cotire limitations
        precompiled.cpp
        precompiled.hpp

    HEADERS
        ../vefs.natvis
        "${_INCLUDE_DIR}/vefs/utils/allocator.hpp"
        "${_INCLUDE_DIR}/vefs/utils/pool_allocator.hpp"
        "${_INCLUDE_DIR}/vefs/utils/secure_ops.hpp"
        "${_INCLUDE_DIR}/vefs/utils/secure_array.hpp"
        "${_INCLUDE_DIR}/vefs/utils/secure_allocator.hpp"

        "${_INCLUDE_DIR}/vefs/utils/misc.hpp"

        "${_INCLUDE_DIR}/vefs/utils/uuid.hpp"
        "${_INCLUDE_DIR}/vefs/utils/random.hpp"

        "${_INCLUDE_DIR}/vefs/utils/enum_bitset.hpp"
        "${_INCLUDE_DIR}/vefs/utils/bit_scan.hpp"
        "${_INCLUDE_DIR}/vefs/utils/bitset_overlay.hpp"

        "${_INCLUDE_DIR}/vefs/utils/hash/algorithm_tag.hpp"
        "${_INCLUDE_DIR}/vefs/utils/hash/default_weak.hpp"
        "${_INCLUDE_DIR}/vefs/utils/hash/detail/std_adaptor.hpp"

        "${_INCLUDE_DIR}/vefs/utils/unordered_map_mt.hpp"

    SOURCES
        secure_ops.cpp

        utf.hpp
        utf.cpp

        proto-helper.hpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(OS_FILESYSTEM_SRC os_filesystem_linux.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(OS_FILESYSTEM_SRC os_filesystem_win.cpp)
endif()

DEF_SOURCE_GROUP(vefs LIB "lib"
    HEADERS
        "${_INCLUDE_DIR}/vefs/archive.hpp"
        "${_INCLUDE_DIR}/vefs/exceptions.hpp"
        "${_INCLUDE_DIR}/vefs/blob.hpp"
        "${_INCLUDE_DIR}/vefs/filesystem.hpp"


    SOURCES
        archive.cpp
        archive_file.hpp
        archive_file.cpp
        filesystem.cpp
        os_filesystem.hpp
        os_filesystem.cpp
        ${OS_FILESYSTEM_SRC}
        exceptions.cpp
)

DEF_SOURCE_GROUP(vefs DETAIL "lib/detail"
    HEADERS
        "${_INCLUDE_DIR}/vefs/detail/cache.hpp"
        "${_INCLUDE_DIR}/vefs/detail/thread_pool.hpp"

        "${_INCLUDE_DIR}/vefs/detail/sector_id.hpp"
        "${_INCLUDE_DIR}/vefs/detail/tree_lut.hpp"
        "${_INCLUDE_DIR}/vefs/detail/tree_walker.hpp"
        "${_INCLUDE_DIR}/vefs/detail/archive_file.hpp"
        "${_INCLUDE_DIR}/vefs/detail/raw_archive.hpp"
        "${_INCLUDE_DIR}/vefs/detail/journal.hpp"

    SOURCES
        thread_pool.cpp
        raw_archive.cpp
        tree_walker.cpp
)

DEF_SOURCE_GROUP(vefs CRYPTO "crypto"
    SOURCES
        sysrandom.cpp
        sysrandom.hpp

        blake2.hpp
        boringssl_aead.hpp

        crypto_provider.cpp
        crypto_provider_debug.hpp
        crypto_provider_boringssl.hpp

        kdf.cpp

        counter.cpp

    HEADERS
        "${_INCLUDE_DIR}/vefs/crypto/provider.hpp"

        "${_INCLUDE_DIR}/vefs/crypto/kdf.hpp"

        "${_INCLUDE_DIR}/vefs/crypto/counter.hpp"
)

DEF_SOURCE_GROUP(vefs EXTERNAL_CUCKOO "ext/libcuckoo"
    HEADERS
        "${_INCLUDE_DIR}/vefs/ext/libcuckoo/cuckoohash_config.hh"
        "${_INCLUDE_DIR}/vefs/ext/libcuckoo/cuckoohash_map.hh"
        "${_INCLUDE_DIR}/vefs/ext/libcuckoo/cuckoohash_util.hh"
        "${_INCLUDE_DIR}/vefs/ext/libcuckoo/libcuckoo_bucket_container.hh"
)
DEF_SOURCE_GROUP(vefs EXTERNAL_CQUEUE "ext/concurrentqueue"
    HEADERS
        "${_INCLUDE_DIR}/vefs/ext/concurrentqueue/concurrentqueue.h"
        "${_INCLUDE_DIR}/vefs/ext/concurrentqueue/blockingconcurrentqueue.h"
)

DEF_SOURCE_GROUP(vefs EXTERNAL_SPOOKYHASH "ext/SpookyV2"
    HEADERS
        "${_INCLUDE_DIR}/vefs/utils/hash/detail/spooky.hpp"
        "${_INCLUDE_DIR}/vefs/utils/hash/detail/SpookyV2_impl.hpp"

    SOURCES
        SpookyV2.cpp
)

DEF_SOURCE_GROUP(vefs PROTO "proto"
    SOURCES
        ${_FILEPROTO_SRCS}
        fileformat.proto

    HEADERS
        ${_FILEPROTO_HDRS}
)

add_library(vefs STATIC
    ${vefs_SRC}
)
GROUP_FILES(vefs)

target_compile_definitions(vefs
    PUBLIC
        BOOST_UUID_NO_TYPE_TRAITS=1
)
target_include_directories(vefs
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PUBLIC
        $<BUILD_INTERFACE:${_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(vefs
    PUBLIC
        advapi32
        Boost::system
        OpenSSL::Crypto
        libb2
        protobuf::libprotobuf
)

if (MSVC)
    set_source_files_properties(${_FILEPROTO_SRCS} PROPERTIES
        COMPILE_FLAGS "/wd4146 /wd4125"
    )
endif()


##########################################################################
# cotire config
set_target_properties(vefs PROPERTIES
    COTIRE_ADD_UNITY_BUILD FALSE
    COTIRE_CXX_PREFIX_HEADER_INIT "precompiled.hpp"
)

cotire(vefs)

##########################################################################
# install targets
install(TARGETS vefs EXPORT vefs-targets
    RUNTIME DESTINATION bin/$<CONFIG>
    LIBRARY DESTINATION lib/$<CONFIG>
    ARCHIVE DESTINATION lib/$<CONFIG>
    INCLUDES DESTINATION include
)
