
########################################################################
# concurrentqueue
add_library(concurrentqueue INTERFACE)
target_include_directories(concurrentqueue INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/concurrentqueue/include>
    $<INSTALL_INTERFACE:include>
)
file(GLOB_RECURSE concurrentqueue_SRC RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/concurrentqueue" concurrentqueue/include/**/*.h)
FOREACH(src IN LISTS concurrentqueue_SRC)
    target_sources(concurrentqueue INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/concurrentqueue/${src}>
        $<INSTALL_INTERFACE:${src}>
    )
ENDFOREACH()
install(DIRECTORY concurrentqueue/include/ DESTINATION include COMPONENT external)


########################################################################
# cuckoohash
add_library(cuckoohash INTERFACE)
target_include_directories(cuckoohash INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cuckoohash/include>
    $<INSTALL_INTERFACE:include>
)
file(GLOB_RECURSE cuckoohash_SRC RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/cuckoohash" cuckoohash/include/**/*.hh)
FOREACH(src IN LISTS cuckoohash_SRC)
    target_sources(cuckoohash INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${src}>
        $<INSTALL_INTERFACE:${src}>
    )
ENDFOREACH()
install(DIRECTORY cuckoohash/include/ DESTINATION include COMPONENT external)
