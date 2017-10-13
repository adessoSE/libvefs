protobuf_generate_cpp(_FILEPROTO_SRCS _FILEPROTO_HDRS fileformat.proto)

# the protobuf generated files don't know about the precompiled header and
# thus cannot be put into the main project
add_library(vefs-proto STATIC
    ${_FILEPROTO_SRCS}
    ${_FILEPROTO_HDRS}

    fileformat.proto
)
target_include_directories(vefs-proto PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
target_link_libraries(vefs-proto PUBLIC protobuf::libprotobuf)

set(_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")

add_library(vefs STATIC
    precompiled.cpp
    precompiled.hpp

    thread_pool.cpp
    "${_INCLUDE_DIR}/vefs/thread_pool.hpp"

    archive.cpp
    "${_INCLUDE_DIR}/vefs/archive.hpp"
    raw_archive.cpp
    "${_INCLUDE_DIR}/vefs/detail/raw_archive.hpp"
    "${_INCLUDE_DIR}/vefs/detail/journal.hpp"

    secure_ops.cpp
    "${_INCLUDE_DIR}/vefs/utils/secure_ops.hpp"
    "${_INCLUDE_DIR}/vefs/utils/secure_array.hpp"
    "${_INCLUDE_DIR}/vefs/utils/secure_allocator.hpp"
    "${_INCLUDE_DIR}/vefs/utils/misc.hpp"

    "${_INCLUDE_DIR}/vefs/blob.hpp"
    "${_INCLUDE_DIR}/vefs/filesystem.hpp"
    filesystem.cpp
    utf.hpp
    utf.cpp

    "${_INCLUDE_DIR}/vefs/detail/enum_bitset.hpp"

    blake2.hpp
    boringssl_aead.hpp
    crypto_provider.cpp
    crypto_provider_debug.hpp
    crypto_provider_boringssl.hpp
    "${_INCLUDE_DIR}/vefs/crypto/provider.hpp"
    kdf.cpp
    "${_INCLUDE_DIR}/vefs/crypto/kdf.hpp"
    counter.cpp
    "${_INCLUDE_DIR}/vefs/crypto/counter.hpp"
)

target_include_directories(vefs
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(vefs
    PUBLIC
        sodium
        Boost::system
        OpenSSL::Crypto
        libb2

    PRIVATE
        vefs-proto
)

##########################################################################
# cotire config
set_target_properties(vefs PROPERTIES
    COTIRE_ADD_UNITY_BUILD FALSE
    COTIRE_CXX_PREFIX_HEADER_INIT "precompiled.hpp"
)

cotire(vefs)

##########################################################################
# install targets
install(TARGETS vefs-proto vefs EXPORT vefs-targets
    RUNTIME DESTINATION bin/$<CONFIG>
    LIBRARY DESTINATION lib/$<CONFIG>
    ARCHIVE DESTINATION lib/$<CONFIG>
    INCLUDES DESTINATION include
)
