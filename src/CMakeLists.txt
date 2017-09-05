
add_library(${PROJECT_NAME} SHARED
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
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX "" SUFFIX ".node"
)

