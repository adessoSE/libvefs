
DEF_SOURCE_GROUP(vefs "core"
    HEADERS
        include/vefs/archive.hpp
        include/vefs/archive_fwd.hpp
        include/vefs/llfio.hpp
        include/vefs/span.hpp

    SOURCES
        # the first added source file determines target language
        # it must therefore be a c++ TU

        src/vefs/archive.cpp
        src/vefs/vfile.cpp
        src/vefs/vfile.hpp
        src/vefs/vfilesystem.cpp
        src/vefs/vfilesystem.hpp
)

DEF_SOURCE_GROUP(vefs "detail"
    SOURCES
        src/vefs/detail/cache_car.hpp
        src/vefs/detail/cache_clock.hpp
        src/vefs/detail/cache_handle.hpp
        src/vefs/detail/cache_page.hpp

        src/vefs/detail/archive_header.hpp
        src/vefs/detail/archive_header.codec.hpp
        src/vefs/detail/archive_file_id.hpp
        src/vefs/detail/block_manager.hpp
        src/vefs/detail/file_crypto_ctx.cpp
        src/vefs/detail/file_crypto_ctx.hpp
        src/vefs/detail/io_buffer_manager.hpp
        src/vefs/detail/reference_sector_layout.hpp
        src/vefs/detail/root_sector_info.cpp
        src/vefs/detail/root_sector_info.hpp
        src/vefs/detail/sector_device.cpp
        src/vefs/detail/sector_device.hpp
        src/vefs/detail/sector_id.hpp
        src/vefs/detail/sector_tree_mt.hpp
        src/vefs/detail/sector_tree_seq.hpp
        src/vefs/detail/tree_lut.hpp
        src/vefs/detail/tree_walker.cpp
        src/vefs/detail/tree_walker.hpp

        src/vefs/detail/file_descriptor.hpp
        src/vefs/detail/file_descriptor.codec.hpp

        src/vefs/detail/archive_sector_allocator.cpp
        src/vefs/detail/archive_sector_allocator.hpp
        src/vefs/detail/archive_tree_allocator.cpp
        src/vefs/detail/archive_tree_allocator.hpp
        src/vefs/detail/cow_tree_allocator_mt.cpp
        src/vefs/detail/cow_tree_allocator_mt.hpp
        src/vefs/detail/preallocated_tree_allocator.cpp
        src/vefs/detail/preallocated_tree_allocator.hpp
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
        src/vefs/disappointment.cpp
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
        src/vefs/crypto/ct_compare.hpp

        src/vefs/crypto/cbor_box.hpp
        src/vefs/crypto/cbor_box.cpp

        src/vefs/crypto/blake2.cpp
        src/vefs/crypto/blake2.hpp
        src/vefs/crypto/boringssl_aead.hpp

        src/vefs/crypto/provider.hpp

        src/vefs/crypto/kdf.cpp
        src/vefs/crypto/kdf.hpp

        src/vefs/crypto/counter.cpp
        src/vefs/crypto/counter.hpp
        src/vefs/crypto/counter.codec.hpp
)
if (boringssl_FOUND)
    DEF_SOURCE_GROUP(vefs "crypto"
        SOURCES
            src/vefs/crypto/crypto_provider_boringssl.cpp
            src/vefs/crypto/crypto_provider_boringssl.hpp
    )
endif()

DEF_SOURCE_GROUP(vefs "platform"
    HEADERS
        include/vefs/platform/platform.hpp
        include/vefs/platform/secure_memzero.hpp
        include/vefs/platform/thread_pool.hpp

    SOURCES
        src/vefs/platform/platform.cpp
        src/vefs/platform/windows-proper.h

        src/vefs/platform/thread_pool.cpp
        src/vefs/platform/thread_pool_gen.hpp
        src/vefs/platform/thread_pool_gen.cpp

        src/vefs/platform/secure_memzero.cpp
        src/vefs/platform/sysrandom.cpp
        src/vefs/platform/sysrandom.hpp
)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    DEF_SOURCE_GROUP(vefs "platform"
        SOURCES
            src/vefs/platform/thread_pool_win32.cpp
            src/vefs/platform/thread_pool_win32.hpp
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

        include/vefs.natvis

    SOURCES
        src/vefs/detail/secure_array.codec.hpp
        src/vefs/detail/uuid.codec.hpp
)

DEF_SOURCE_GROUP(vefs "external/SpookyV2"
    HEADERS
        include/vefs/utils/hash/detail/spooky.hpp
        include/vefs/utils/hash/detail/SpookyV2_impl.hpp

    SOURCES
        src/vefs/detail/SpookyV2.cpp
)

if (BUILD_TESTING)
    target_sources(vefs-tests PRIVATE
        tests/vefs-tests.cpp
        tests/boost-unit-test.hpp
        tests/test-utils.hpp
        tests/test-utils.cpp
        tests/libb2_none_blake2b_crypto_provider.cpp
        tests/libb2_none_blake2b_crypto_provider.hpp
        tests/disappointment-tests.cpp
        tests/io_buffer_manager.test.cpp
        tests/allocator-tests.cpp
        tests/cache-tests.cpp
        tests/cache_clock-tests.cpp
        tests/sector_device-tests.cpp
        tests/sector_tree_mt-tests.cpp
        tests/sector_tree_seq-tests.cpp
        tests/archive-tests.cpp
        tests/vfile-tests.cpp
        tests/vfilesystem-tests.cpp
        tests/span-tests.cpp
        tests/crypto_provider-tests.cpp
        tests/block_manager-tests.cpp
        tests/tree_lut-tests.cpp
        tests/tree_walker-tests.cpp
        tests/archive_file_id_tests.cpp
        tests/archive-integration-tests.cpp
     )
endif()
