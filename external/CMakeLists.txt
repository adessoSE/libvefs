
function(add_ext_interface_library NAME)

    add_library("${NAME}" INTERFACE)

    target_include_directories("${NAME}" INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${NAME}/include>
        $<INSTALL_INTERFACE:include>
    )

    install(DIRECTORY "${NAME}/include/" DESTINATION include COMPONENT external)

endfunction()

add_ext_interface_library(concurrentqueue)
add_ext_interface_library(cuckoohash)
