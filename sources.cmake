
dplx_target_sources(vefs # core
    MODE VERBATIM
    BASE_DIR vefs

    PUBLIC
        archive.hpp
        archive_fwd.hpp
        llfio.hpp
        span.hpp
        
        config.hpp
        detail/workaround.hpp

    PRIVATE
        # the first added source file determines target language
        # it must therefore be a c++ TU

        archive.cpp
        vfile.cpp
        vfile.hpp
        vfilesystem.cpp
        vfilesystem.hpp
)

dplx_target_sources(vefs # detail
    MODE VERBATIM
    BASE_DIR vefs

    PRIVATE
        detail/archive_header.cpp
        detail/archive_header.hpp
        detail/archive_file_id.hpp
        detail/block_manager.hpp
        detail/file_crypto_ctx.cpp
        detail/file_crypto_ctx.hpp
        detail/io_buffer_manager.hpp
        detail/reference_sector_layout.hpp
        detail/root_sector_info.cpp
        detail/root_sector_info.hpp
        detail/sector_device.cpp
        detail/sector_device.hpp
        detail/sector_id.hpp
        detail/sector_tree_mt.hpp
        detail/sector_tree_seq.hpp
        detail/tree_lut.hpp
        detail/tree_walker.cpp
        detail/tree_walker.hpp

        detail/file_descriptor.cpp
        detail/file_descriptor.hpp

        detail/archive_sector_allocator.cpp
        detail/archive_sector_allocator.hpp
        detail/archive_tree_allocator.cpp
        detail/archive_tree_allocator.hpp
        detail/cow_tree_allocator_mt.cpp
        detail/cow_tree_allocator_mt.hpp
        detail/preallocated_tree_allocator.cpp
        detail/preallocated_tree_allocator.hpp
)

dplx_target_sources(vefs # cache
    MODE VERBATIM
    BASE_DIR vefs

    PRIVATE
        cache/bloom_filter.cpp
        cache/bloom_filter.hpp
        cache/spectral_bloom_filter.cpp
        cache/spectral_bloom_filter.hpp

        cache/cache_mt.cpp
        cache/cache_mt.hpp
        cache/cache_page.cpp
        cache/cache_page.hpp
        cache/eviction_policy.cpp
        cache/eviction_policy.hpp
        cache/lru_policy.cpp
        cache/lru_policy.hpp
        cache/slru_policy.cpp
        cache/slru_policy.hpp
        cache/w-tinylfu_policy.cpp
        cache/w-tinylfu_policy.hpp
)

dplx_target_sources(vefs # disappointment
    MODE VERBATIM
    BASE_DIR vefs

    PUBLIC
        exceptions.hpp

        disappointment.hpp
        disappointment/errc.hpp
        disappointment/error_detail.hpp
        disappointment/fwd.hpp
        disappointment/generic_errc.hpp

    PRIVATE
        disappointment.cpp
)

dplx_target_sources(vefs # crypto
    MODE VERBATIM
    BASE_DIR vefs

    PUBLIC
        crypto/provider.hpp

    PRIVATE
        crypto/ct_compare.hpp

        crypto/cbor_box.hpp
        crypto/cbor_box.cpp

        crypto/blake2.cpp
        crypto/blake2.hpp
        crypto/boringssl_aead.hpp

        crypto/kdf.cpp
        crypto/kdf.hpp

        crypto/counter.cpp
        crypto/counter.hpp
)
if (OpenSSL_FOUND)
    dplx_target_sources(vefs # crypto
        MODE VERBATIM
        BASE_DIR vefs

        PRIVATE
            crypto/crypto_provider_boringssl.cpp
            crypto/crypto_provider_boringssl.hpp
    )
endif()

dplx_target_sources(vefs # platform
    MODE VERBATIM
    BASE_DIR vefs

    PUBLIC
        platform/platform.hpp
        platform/secure_memzero.hpp
        platform/thread_pool.hpp

    PRIVATE
        platform/platform.cpp
        platform/windows-proper.h

        platform/thread_pool.cpp
        platform/thread_pool_gen.hpp
        platform/thread_pool_gen.cpp

        platform/secure_memzero.cpp
        platform/sysrandom.cpp
        platform/sysrandom.hpp
)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    dplx_target_sources(vefs # platform
        MODE VERBATIM
        BASE_DIR vefs

        PRIVATE
            platform/thread_pool_win32.cpp
            platform/thread_pool_win32.hpp
    )
endif()

dplx_target_sources(vefs # utils
    MODE VERBATIM
    BASE_DIR vefs

    PUBLIC
        utils/secure_array.hpp
        utils/secure_allocator.hpp

        utils/workaround.h

        utils/misc.hpp
        utils/ref_ptr.hpp
        utils/dirt_flag.hpp
        utils/object_storage.hpp

        utils/uuid.hpp
        utils/random.hpp

        utils/binary_codec.hpp
        utils/bit_scan.hpp
        utils/bitset_overlay.hpp
        utils/enum_bitset.hpp

        utils/unordered_map_mt.hpp

    PRIVATE
        detail/uuid.cpp
)

dplx_target_sources(vefs # hash
    MODE VERBATIM
    BASE_DIR vefs

    PUBLIC
        hash/detail/spooky_v2_impl.hpp
        hash/hash_algorithm.hpp
        hash/hash-std.hpp
        hash/spooky_v2.hpp

    PRIVATE
        hash/detail/spooky_v2_impl.cpp
        hash/hash_algorithm.cpp
        hash/hash-std.cpp
        hash/spooky_v2.cpp
)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/src/vefs/detail")
configure_file(tools/config.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/src/vefs/detail/config.hpp" @ONLY)
target_sources(vefs PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/src/vefs/detail/config.hpp>)

if (BUILD_TESTING)
    dplx_target_sources(vefs-tests PRIVATE
        MODE VERBATIM
        BASE_DIR ../tests

        PRIVATE
            vefs-tests.cpp

            test_utils/boost-unit-test.hpp
            test_utils/libb2_none_blake2b_crypto_provider.cpp
            test_utils/libb2_none_blake2b_crypto_provider.hpp
            test_utils/mocks.hpp
            test_utils/test-utils.cpp
            test_utils/test-utils.hpp

            io_buffer_manager.test.cpp
            sector_device-tests.cpp
            sector_tree_mt-tests.cpp
            sector_tree_seq-tests.cpp
            archive-tests.cpp
            vfile-tests.cpp
            vfilesystem-tests.cpp
            span-tests.cpp
            crypto_provider-tests.cpp
            block_manager-tests.cpp
            tree_lut-tests.cpp
            tree_walker-tests.cpp
            archive_file_id_tests.cpp
            archive-integration-tests.cpp

            vefs/hash/hash_algorithm.test.cpp
            vefs/hash/spooky_v2.test.cpp

            vefs/cache/bloom_filter.test.cpp
            vefs/cache/cache_mt.test.cpp
            vefs/cache/cache_page.test.cpp
            vefs/cache/eviction_policy.test.cpp
            vefs/cache/lru_policy.test.cpp
            vefs/cache/slru_policy.test.cpp
            vefs/cache/spectral_bloom_filter.test.cpp
            vefs/cache/w-tinylfu_policy.test.cpp
     )
endif()
