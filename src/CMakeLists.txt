protobuf_generate_cpp(_FILEPROTO_SRCS _FILEPROTO_HDRS fileformat.proto)

# the protobuf generated files don't know about the precompiled header and
# thus cannot be put into the main project
add_library(vefs-proto STATIC
    ${_FILEPROTO_SRCS}
    ${_FILEPROTO_HDRS}
)
target_include_directories(vefs-proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(vefs-proto PUBLIC protobuf::libprotobuf)

add_library(vefs STATIC
    precompiled.cpp
    precompiled.hpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        sodium
        Boost::system

    PRIVATE
        vefs-proto
)

##########################################################################
# cotire config
set_target_properties(${PROJECT_NAME} PROPERTIES
    COTIRE_ADD_UNITY_BUILD FALSE
    COTIRE_CXX_PREFIX_HEADER_INIT "precompiled.hpp"
)

cotire(${PROJECT_NAME})

