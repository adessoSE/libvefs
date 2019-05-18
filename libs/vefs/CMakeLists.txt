
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(OS_FILESYSTEM_SRC src/os_filesystem_linux.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(OS_FILESYSTEM_SRC src/os_filesystem_win.cpp)
endif()

add_library(vefs)

DEF_SOURCE_GROUP(vefs "utils"
    SOURCES
        # the precompiled.cpp _must_ be the first source file
        # due to cotire limitations
        src/precompiled.cpp
        src/precompiled.hpp

    HEADERS
        vefs.natvis
        include/vefs/utils/secure_ops.hpp
        include/vefs/utils/secure_array.hpp
        include/vefs/utils/secure_allocator.hpp

        include/vefs/utils/misc.hpp
        include/vefs/utils/ref_ptr.hpp
        include/vefs/utils/dirt_flag.hpp
        include/vefs/utils/platform.hpp
        include/vefs/utils/windows-proper.h

        include/vefs/utils/uuid.hpp
        include/vefs/utils/random.hpp

        include/vefs/utils/enum_bitset.hpp
        include/vefs/utils/bit_scan.hpp
        include/vefs/utils/bitset_overlay.hpp

        include/vefs/utils/hash/algorithm_tag.hpp
        include/vefs/utils/hash/default_weak.hpp
        include/vefs/utils/hash/detail/std_adaptor.hpp

        include/vefs/utils/unordered_map_mt.hpp

    SOURCES
        src/platform.cpp

        src/secure_ops.cpp

        src/block_manager.hpp

        src/utf.hpp
        src/utf.cpp

        src/proto-helper.hpp
)

DEF_SOURCE_GROUP(vefs "lib"
    HEADERS
        include/vefs/archive.hpp
        include/vefs/archive_fwd.hpp
        include/vefs/exceptions.hpp
        include/vefs/blob.hpp
        include/vefs/filesystem.hpp

    SOURCES
        src/archive.cpp
        src/archive_file.hpp
        src/archive_file.cpp
        src/archive_file_lookup.hpp
        src/archive_file_lookup.cpp
        src/archive_free_block_list_file.hpp
        src/archive_free_block_list_file.cpp
        src/archive_index_file.hpp
        src/archive_index_file.cpp
        src/archive_internal_file.hpp
        src/archive_internal_file.cpp

        src/filesystem.cpp
        src/os_filesystem.hpp
        src/os_filesystem.cpp
        ${OS_FILESYSTEM_SRC}
)

DEF_SOURCE_GROUP(vefs "lib/disappointment"
    HEADERS
        include/vefs/disappointment.hpp
        include/vefs/disappointment/errc.hpp
        include/vefs/disappointment/error.hpp
        include/vefs/disappointment/error_detail.hpp
        include/vefs/disappointment/error_domain.hpp
        include/vefs/disappointment/error_exception.hpp
        include/vefs/disappointment/fwd.hpp
        include/vefs/disappointment/generic_errc.hpp
        include/vefs/disappointment/std_adapter.hpp

    SOURCES
        src/disappointment.cpp
)

DEF_SOURCE_GROUP(vefs "lib/detail"
    HEADERS
        include/vefs/detail/cache.hpp
        include/vefs/detail/thread_pool.hpp
        include/vefs/detail/thread_pool_win32.hpp
        include/vefs/detail/thread_pool_gen.hpp

        include/vefs/detail/archive_file_id.hpp
        include/vefs/detail/basic_archive_file_meta.hpp
        include/vefs/detail/journal.hpp
        include/vefs/detail/raw_archive.hpp
        include/vefs/detail/sector_id.hpp
        include/vefs/detail/tree_lut.hpp
        include/vefs/detail/tree_walker.hpp

    SOURCES
        src/thread_pool.cpp
        src/thread_pool_gen.cpp
        src/thread_pool_win32.cpp
        src/raw_archive.cpp
        src/tree_walker.cpp
)

DEF_SOURCE_GROUP(vefs "lib/allocator"
    HEADERS
        include/vefs/allocator/alignment.hpp
        include/vefs/allocator/allocation.hpp
        include/vefs/allocator/multi_pool_mt.hpp
        include/vefs/allocator/octopus.hpp
        include/vefs/allocator/pool_mt.hpp
        include/vefs/allocator/std_adapter.hpp
        include/vefs/allocator/system.hpp

        include/vefs/allocator/atomic_ring_counter.hpp
        include/vefs/allocator/atomic_resource_counter.hpp
)

DEF_SOURCE_GROUP(vefs "crypto"
    SOURCES
        src/sysrandom.cpp
        src/sysrandom.hpp

        src/ct_compare.hpp

        src/blake2.cpp
        src/blake2.hpp
        src/boringssl_aead.hpp

        src/crypto_provider_debug.cpp

        src/kdf.cpp

        src/counter.cpp

    HEADERS
        include/vefs/crypto/provider.hpp

        include/vefs/crypto/kdf.hpp

        include/vefs/crypto/counter.hpp
)
if (boringssl_FOUND)
    DEF_SOURCE_GROUP(vefs "crypto"
        SOURCES
            src/crypto_provider_boringssl.cpp
    )
endif()

DEF_SOURCE_GROUP(vefs "ext/libcuckoo"
    HEADERS
        include/vefs/ext/libcuckoo/cuckoohash_config.hh
        include/vefs/ext/libcuckoo/cuckoohash_map.hh
        include/vefs/ext/libcuckoo/cuckoohash_util.hh
        include/vefs/ext/libcuckoo/libcuckoo_bucket_container.hh
)
DEF_SOURCE_GROUP(vefs "ext/concurrentqueue"
    HEADERS
        include/vefs/ext/concurrentqueue/concurrentqueue.h
        include/vefs/ext/concurrentqueue/blockingconcurrentqueue.h
)

DEF_SOURCE_GROUP(vefs "ext/SpookyV2"
    HEADERS
        include/vefs/utils/hash/detail/spooky.hpp
        include/vefs/utils/hash/detail/SpookyV2_impl.hpp

    SOURCES
        src/SpookyV2.cpp
)

target_sources(vefs PRIVATE fileformat.proto)
protobuf_generate(TARGET vefs)

target_compile_definitions(vefs
    PUBLIC
        BOOST_UUID_NO_TYPE_TRAITS=1
)
target_include_directories(vefs
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(vefs
    PUBLIC
        advapi32
        Boost::boost
        Boost::system
        libb2
        protobuf::libprotobuf
        fmt::fmt
)

if (botan_FOUND)
    target_link_libraries(vefs PUBLIC botan)
endif()
if (boringssl_FOUND)
    target_link_libraries(vefs PUBLIC boringssl::crypto)
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
    COTIRE_CXX_PREFIX_HEADER_INIT "src/precompiled.hpp"
)

cotire(vefs)

##########################################################################
# test target
add_executable(vefs-tests
    tests/vefs-tests.cpp
    tests/boost-unit-test.hpp
    tests/test-utils.hpp

    tests/disappointment-tests.cpp
    tests/allocator-tests.cpp
    tests/cache-tests.cpp

    tests/osfilesystem-tests.cpp
    tests/memfs.cpp
    tests/memfs-tests.cpp
    tests/memfs.hpp

    tests/raw_archive-tests.cpp
    tests/archive-tests.cpp

    tests/blob-tests.cpp
    tests/crypto_provider-tests.cpp
)
target_link_libraries(vefs-tests
    PUBLIC
        vefs
        Boost::unit_test_framework
)
add_test(NAME vefs-tests COMMAND vefs-tests)


##########################################################################
# install targets
install(TARGETS vefs EXPORT vefs-targets
    RUNTIME DESTINATION bin/$<CONFIG>
    LIBRARY DESTINATION lib/$<CONFIG>
    ARCHIVE DESTINATION lib/$<CONFIG>
    INCLUDES DESTINATION include
)
