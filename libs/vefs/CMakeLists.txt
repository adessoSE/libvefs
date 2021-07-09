
add_library(vefs)

DEF_SOURCE_GROUP(vefs "core"
    HEADERS
        include/vefs/archive.hpp
        include/vefs/archive_fwd.hpp
        include/vefs/llfio.hpp
        include/vefs/span.hpp

    SOURCES
        # the first added source file determines target language
        # it must therefore be a c++ TU

        src/archive.cpp
        src/vfile.cpp
        src/vfile.hpp
        src/vfilesystem.cpp
        src/vfilesystem.hpp
)

DEF_SOURCE_GROUP(vefs "detail"
    SOURCES
        src/detail/cache_car.hpp
        src/detail/cache_clock.hpp
        src/detail/cache_handle.hpp
        src/detail/cache_page.hpp

        src/detail/archive_header.hpp
        src/detail/archive_header.codec.hpp
        src/detail/archive_file_id.hpp
        src/detail/block_manager.hpp
        src/detail/file_crypto_ctx.cpp
        src/detail/file_crypto_ctx.hpp
        src/detail/reference_sector_layout.hpp
        src/detail/root_sector_info.cpp
        src/detail/root_sector_info.hpp
        src/detail/sector_device.cpp
        src/detail/sector_device.hpp
        src/detail/sector_id.hpp
        src/detail/sector_tree_mt.hpp
        src/detail/sector_tree_seq.hpp
        src/detail/tree_lut.hpp
        src/detail/tree_walker.cpp
        src/detail/tree_walker.hpp

        src/detail/file_descriptor.hpp
        src/detail/file_descriptor.codec.hpp

        src/detail/archive_sector_allocator.cpp
        src/detail/archive_sector_allocator.hpp
        src/detail/archive_tree_allocator.cpp
        src/detail/archive_tree_allocator.hpp
        src/detail/cow_tree_allocator_mt.cpp
        src/detail/cow_tree_allocator_mt.hpp
        src/detail/preallocated_tree_allocator.cpp
        src/detail/preallocated_tree_allocator.hpp
)

DEF_SOURCE_GROUP(vefs "disappointment"
    HEADERS
        include/vefs/exceptions.hpp

        include/vefs/disappointment.hpp
        include/vefs/disappointment/errc.hpp
        include/vefs/disappointment/error.hpp
        include/vefs/disappointment/error_detail.hpp
        include/vefs/disappointment/error_domain.hpp
        include/vefs/disappointment/error_exception.hpp
        include/vefs/disappointment/fwd.hpp
        include/vefs/disappointment/generic_errc.hpp
        include/vefs/disappointment/llfio_adapter.hpp
        include/vefs/disappointment/std_adapter.hpp

    SOURCES
        src/disappointment.cpp
)

DEF_SOURCE_GROUP(vefs "allocator"
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
        src/crypto/ct_compare.hpp

        src/crypto/cbor_box.hpp
        src/crypto/cbor_box.cpp

        src/crypto/blake2.cpp
        src/crypto/blake2.hpp
        src/crypto/boringssl_aead.hpp

        src/crypto/provider.hpp

        src/crypto/kdf.cpp
        src/crypto/kdf.hpp

        src/crypto/counter.cpp
        src/crypto/counter.hpp
        src/crypto/counter.codec.hpp
)
if (boringssl_FOUND)
    DEF_SOURCE_GROUP(vefs "crypto"
        SOURCES
            src/crypto/crypto_provider_boringssl.cpp
            src/crypto/crypto_provider_boringssl.hpp
    )
endif()

DEF_SOURCE_GROUP(vefs "platform"
    HEADERS
        include/vefs/platform/platform.hpp
        include/vefs/platform/secure_memzero.hpp
        include/vefs/platform/thread_pool.hpp

    SOURCES
        src/platform/platform.cpp
        src/platform/windows-proper.h

        
        src/platform/thread_pool.cpp
        src/platform/thread_pool_gen.hpp
        src/platform/thread_pool_gen.cpp

        src/platform/secure_memzero.cpp
        src/platform/sysrandom.cpp
        src/platform/sysrandom.hpp
)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    DEF_SOURCE_GROUP(vefs "platform"
        SOURCES
            src/platform/thread_pool_win32.cpp
            src/platform/thread_pool_win32.hpp
    )
endif()

DEF_SOURCE_GROUP(vefs "utils"
    HEADERS
        include/vefs/utils/secure_array.hpp
        include/vefs/utils/secure_allocator.hpp

        include/vefs/utils/misc.hpp
        include/vefs/utils/ref_ptr.hpp
        include/vefs/utils/dirt_flag.hpp
        include/vefs/utils/object_storage.hpp

        include/vefs/utils/uuid.hpp
        include/vefs/utils/random.hpp

        include/vefs/utils/binary_codec.hpp
        include/vefs/utils/bit_scan.hpp
        include/vefs/utils/bitset_overlay.hpp
        include/vefs/utils/enum_bitset.hpp

        include/vefs/utils/hash/algorithm_tag.hpp
        include/vefs/utils/hash/default_weak.hpp
        include/vefs/utils/hash/detail/std_adaptor.hpp

        include/vefs/utils/unordered_map_mt.hpp

        vefs.natvis

    SOURCES
        src/detail/secure_array.codec.hpp
        src/detail/uuid.codec.hpp
)

DEF_SOURCE_GROUP(vefs "external/SpookyV2"
    HEADERS
        include/vefs/utils/hash/detail/spooky.hpp
        include/vefs/utils/hash/detail/SpookyV2_impl.hpp

    SOURCES
        src/detail/SpookyV2.cpp
)

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
        $<$<PLATFORM_ID:Windows>:advapi32>
        Boost::boost
        Boost::system
        Deeplex::deeppack
        libb2
        fmt::fmt
        llfio::llfio
        outcome::outcome
        unofficial::concurrentqueue::concurrentqueue
        libcuckoo
)

if (botan_FOUND)
    target_link_libraries(vefs PUBLIC botan)
endif()
if (boringssl_FOUND)
    target_link_libraries(vefs PUBLIC boringssl::crypto)
endif()

if (MSVC_COMPATIBLE)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/fileformat.pb.cc" PROPERTIES
        COMPILE_FLAGS "/wd4100 /wd4125 /wd4127 /wd4146 /wd4244"
    )
endif()

##########################################################################
# test target
if (BUILD_TESTING)
    add_executable(vefs-tests
        tests/vefs-tests.cpp
        tests/boost-unit-test.hpp
        tests/test-utils.hpp
        tests/test-utils.cpp
        tests/libb2_none_blake2b_crypto_provider.cpp
        tests/libb2_none_blake2b_crypto_provider.hpp
        tests/disappointment-tests.cpp
        tests/allocator-tests.cpp
        tests/cache-tests.cpp
        tests/cache_clock-tests.cpp
        tests/sector_device-tests.cpp
        tests/sector_tree_mt-tests.cpp
        tests/sector_tree_seq-tests.cpp
        tests/archive-tests.cpp
        tests/vfilesystem-tests.cpp
        tests/span-tests.cpp
        tests/crypto_provider-tests.cpp
        tests/block_manager-tests.cpp
        tests/tree_lut-tests.cpp
        tests/tree_walker-tests.cpp
        tests/archive_file_id_tests.cpp
        tests/archive-integration-tests.cpp
     )
    target_link_libraries(vefs-tests
        PUBLIC
            vefs
            Boost::unit_test_framework
            GTest::gmock
    )
    add_test(NAME vefs-tests COMMAND vefs-tests)
endif()


##########################################################################
# install targets
install(TARGETS vefs EXPORT vefs-targets
    RUNTIME DESTINATION bin/$<CONFIG>
    LIBRARY DESTINATION lib/$<CONFIG>
    ARCHIVE DESTINATION lib/$<CONFIG>
)
install(DIRECTORY include/ DESTINATION include COMPONENT core)
