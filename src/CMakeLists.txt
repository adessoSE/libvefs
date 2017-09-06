
protobuf_generate_cpp(_FILEPROTO_SRCS _FILEPROTO_HDRS fileformat.proto)

# the protobuf generated files don't know about the precompiled header and
# thus cannot be put into the main project
add_library(fileprotomsgs
    ${_FILEPROTO_SRCS}
    ${_FILEPROTO_HDRS}
)
target_include_directories(fileprotomsgs PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(fileprotomsgs PUBLIC protobuf::libprotobuf)

add_library(${PROJECT_NAME} SHARED
    precompiled.cpp
    precompiled.hpp

    module.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_JS_INC}
        "${CMAKE_SOURCE_DIR}/node_modules/node-addon-api"
)
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_JS_LIB}
        fileprotomsgs
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX "" SUFFIX ".node"
)

##########################################################################
# cotire config
set_target_properties(${PROJECT_NAME} PROPERTIES
    COTIRE_ADD_UNITY_BUILD FALSE
    COTIRE_CXX_PREFIX_HEADER_INIT "precompiled.hpp"
)

cotire(${PROJECT_NAME})

