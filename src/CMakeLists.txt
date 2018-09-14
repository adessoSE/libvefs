
set(_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(OS_FILESYSTEM_SRC os_filesystem_linux.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(OS_FILESYSTEM_SRC os_filesystem_win.cpp)
endif()

add_library(vefs STATIC
    ${vefs_SRC}

    fileformat.proto
)

DEF_SOURCE_GROUP(vefs "utils"
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
        "${_INCLUDE_DIR}/vefs/utils/ref_ptr.hpp"
        "${_INCLUDE_DIR}/vefs/utils/dirt_flag.hpp"

        "${_INCLUDE_DIR}/vefs/utils/uuid.hpp"
        "${_INCLUDE_DIR}/vefs/utils/random.hpp"

        "${_INCLUDE_DIR}/vefs/utils/enum_bitset.hpp"
        "${_INCLUDE_DIR}/vefs/utils/bit_scan.hpp"
        "${_INCLUDE_DIR}/vefs/utils/bitset_overlay.hpp"

        "${_INCLUDE_DIR}/vefs/utils/hash/algorithm_tag.hpp"
        "${_INCLUDE_DIR}/vefs/utils/hash/default_weak.hpp"
        "${_INCLUDE_DIR}/vefs/utils/hash/detail/std_adaptor.hpp"

        "${_INCLUDE_DIR}/vefs/utils/unordered_map_mt.hpp"

        "${_INCLUDE_DIR}/vefs/utils/async_error_info.hpp"

    SOURCES
        secure_ops.cpp

        block_manager.hpp

        utf.hpp
        utf.cpp

        proto-helper.hpp
)

DEF_SOURCE_GROUP(vefs "lib"
    HEADERS
        "${_INCLUDE_DIR}/vefs/archive.hpp"
        "${_INCLUDE_DIR}/vefs/archive_fwd.hpp"
        "${_INCLUDE_DIR}/vefs/exceptions.hpp"
        "${_INCLUDE_DIR}/vefs/blob.hpp"
        "${_INCLUDE_DIR}/vefs/filesystem.hpp"

    SOURCES
        archive.cpp
        archive_file.hpp
        archive_file.cpp
        archive_file_lookup.hpp
        archive_file_lookup.cpp
        archive_free_block_list_file.hpp
        archive_free_block_list_file.cpp
        archive_index_file.hpp
        archive_index_file.cpp
        archive_internal_file.hpp
        archive_internal_file.cpp

        filesystem.cpp
        os_filesystem.hpp
        os_filesystem.cpp
        ${OS_FILESYSTEM_SRC}
        exceptions.cpp
)

DEF_SOURCE_GROUP(vefs "lib/detail"
    HEADERS
        "${_INCLUDE_DIR}/vefs/detail/cache.hpp"
        "${_INCLUDE_DIR}/vefs/detail/thread_pool.hpp"

        "${_INCLUDE_DIR}/vefs/detail/archive_file_id.hpp"
        "${_INCLUDE_DIR}/vefs/detail/basic_archive_file_meta.hpp"
        "${_INCLUDE_DIR}/vefs/detail/journal.hpp"
        "${_INCLUDE_DIR}/vefs/detail/raw_archive.hpp"
        "${_INCLUDE_DIR}/vefs/detail/sector_id.hpp"
        "${_INCLUDE_DIR}/vefs/detail/tree_lut.hpp"
        "${_INCLUDE_DIR}/vefs/detail/tree_walker.hpp"

    SOURCES
        thread_pool.cpp
        raw_archive.cpp
        tree_walker.cpp
)

DEF_SOURCE_GROUP(vefs "crypto"
    SOURCES
        sysrandom.cpp
        sysrandom.hpp

        ct_compare.hpp

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

DEF_SOURCE_GROUP(vefs "ext/libcuckoo"
    HEADERS
        "${_INCLUDE_DIR}/vefs/ext/libcuckoo/cuckoohash_config.hh"
        "${_INCLUDE_DIR}/vefs/ext/libcuckoo/cuckoohash_map.hh"
        "${_INCLUDE_DIR}/vefs/ext/libcuckoo/cuckoohash_util.hh"
        "${_INCLUDE_DIR}/vefs/ext/libcuckoo/libcuckoo_bucket_container.hh"
)
DEF_SOURCE_GROUP(vefs "ext/concurrentqueue"
    HEADERS
        "${_INCLUDE_DIR}/vefs/ext/concurrentqueue/concurrentqueue.h"
        "${_INCLUDE_DIR}/vefs/ext/concurrentqueue/blockingconcurrentqueue.h"
)

DEF_SOURCE_GROUP(vefs "ext/SpookyV2"
    HEADERS
        "${_INCLUDE_DIR}/vefs/utils/hash/detail/spooky.hpp"
        "${_INCLUDE_DIR}/vefs/utils/hash/detail/SpookyV2_impl.hpp"

    SOURCES
        SpookyV2.cpp
)

protobuf_generate(TARGET vefs)

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
        libb2
        protobuf::libprotobuf
)

if (botan_FOUND)
    target_link_libraries(vefs PUBLIC botan)
endif()
if (OpenSSL_FOUND)
    target_link_libraries(vefs PUBLIC OpenSSL::Crypto)
endif()

if (MSVC)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/fileformat.pb.cc" PROPERTIES
        COMPILE_FLAGS "/wd4100 /wd4125 /wd4146"
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
