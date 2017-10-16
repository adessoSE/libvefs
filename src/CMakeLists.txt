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

DEF_SOURCE_GROUP(vefs UTILS "utils"
    SOURCES
        # the precompiled.cpp _must_ be the first source file
        # due to cotire limitations
        precompiled.cpp
        precompiled.hpp

    HEADERS
        "${_INCLUDE_DIR}/vefs/utils/secure_ops.hpp"
        "${_INCLUDE_DIR}/vefs/utils/secure_array.hpp"
        "${_INCLUDE_DIR}/vefs/utils/secure_allocator.hpp"

        "${_INCLUDE_DIR}/vefs/utils/misc.hpp"

        "${_INCLUDE_DIR}/vefs/utils/enum_bitset.hpp"

    SOURCES
        secure_ops.cpp

        utf.hpp
        utf.cpp
)

DEF_SOURCE_GROUP(vefs LIB "lib"
    HEADERS
        "${_INCLUDE_DIR}/vefs/archive.hpp"
        "${_INCLUDE_DIR}/vefs/exceptions.hpp"
        "${_INCLUDE_DIR}/vefs/blob.hpp"
        "${_INCLUDE_DIR}/vefs/filesystem.hpp"


    SOURCES
        archive.cpp
        filesystem.cpp
)

DEF_SOURCE_GROUP(vefs DETAIL "lib/detail"
    HEADERS
        "${_INCLUDE_DIR}/vefs/detail/thread_pool.hpp"
        "${_INCLUDE_DIR}/vefs/detail/raw_archive.hpp"
        "${_INCLUDE_DIR}/vefs/detail/journal.hpp"

    SOURCES
        thread_pool.cpp
        raw_archive.cpp
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

add_library(vefs STATIC
    ${vefs_SRC}
)
GROUP_FILES(vefs)

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
