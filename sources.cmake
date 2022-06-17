
dplx_target_sources(vefs # core
    MODE VERBATIM
    BASE_DIR vefs

    PUBLIC
        archive.hpp
        archive_fwd.hpp
        llfio.hpp
        span.hpp

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
        detail/cache_car.hpp
        detail/cache_clock.hpp
        detail/cache_handle.hpp
        detail/cache_page.hpp

        detail/archive_header.hpp
        detail/archive_header.codec.hpp
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

        detail/file_descriptor.hpp
        detail/file_descriptor.codec.hpp

        detail/archive_sector_allocator.cpp
        detail/archive_sector_allocator.hpp
        detail/archive_tree_allocator.cpp
        detail/archive_tree_allocator.hpp
        detail/cow_tree_allocator_mt.cpp
        detail/cow_tree_allocator_mt.hpp
        detail/preallocated_tree_allocator.cpp
        detail/preallocated_tree_allocator.hpp
)

dplx_target_sources(vefs # disappointment
    MODE VERBATIM
    BASE_DIR vefs

    PUBLIC
        exceptions.hpp

        disappointment.hpp
        disappointment/errc.hpp
        disappointment/error.hpp
        disappointment/error_detail.hpp
        disappointment/error_domain.hpp
        disappointment/error_exception.hpp
        disappointment/fwd.hpp
        disappointment/generic_errc.hpp
        disappointment/llfio_adapter.hpp
        disappointment/std_adapter.hpp

    PRIVATE
        disappointment.cpp
)

dplx_target_sources(vefs # allocator
    MODE VERBATIM
    BASE_DIR vefs

    PUBLIC
        allocator/alignment.hpp
        allocator/allocation.hpp
        allocator/multi_pool_mt.hpp
        allocator/octopus.hpp
        allocator/pool_mt.hpp
        allocator/std_adapter.hpp
        allocator/system.hpp

        allocator/atomic_ring_counter.hpp
        allocator/atomic_resource_counter.hpp
)

dplx_target_sources(vefs # crypto
    MODE VERBATIM
    BASE_DIR vefs

    PRIVATE
        crypto/ct_compare.hpp

        crypto/cbor_box.hpp
        crypto/cbor_box.cpp

        crypto/blake2.cpp
        crypto/blake2.hpp
        crypto/boringssl_aead.hpp

        crypto/provider.hpp

        crypto/kdf.cpp
        crypto/kdf.hpp

        crypto/counter.cpp
        crypto/counter.hpp
        crypto/counter.codec.hpp
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

        utils/hash/algorithm_tag.hpp
        utils/hash/default_weak.hpp
        utils/hash/detail/std_adaptor.hpp

        utils/unordered_map_mt.hpp

    PRIVATE
        detail/secure_array.codec.hpp
        detail/uuid.codec.hpp
)

dplx_target_sources(vefs # hash
    MODE VERBATIM
    BASE_DIR vefs

    PUBLIC
        hash/detail/spooky_v2_impl.hpp
        utils/hash/detail/spooky.hpp

    PRIVATE
        hash/detail/spooky_v2_impl.cpp
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
