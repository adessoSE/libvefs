
function(dplx_target_sources TARGET)
    set(OPTIONS MERGED_LAYOUT)
    set(ONE_VALUE_ARGS MODE TEST_TARGET BASE_DIR)
    set(MULTI_VALUE_ARGS PRIVATE PUBLIC)
    cmake_parse_arguments(PARSE_ARGV 1 _
        "${OPTIONS}"
        "${ONE_VALUE_ARGS}"
        "${MULTI_VALUE_ARGS}")

    if (NOT DEFINED __MODE)
        set(__MODE VERBATIM)
    endif ()

    cmake_path(SET PRIVATE_BASE NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/src/${__BASE_DIR}")
    if (__MERGED_LAYOUT)
        cmake_path(SET PUBLIC_BASE NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/src/${__BASE_DIR}")
    else ()
        cmake_path(SET PUBLIC_BASE NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/include/${__BASE_DIR}")
    endif ()

    set(SOURCES) # shadow variable from outer scopes
    foreach(type PRIVATE PUBLIC)
        foreach(file IN LISTS __${type})
            cmake_path(SET FILE_PATH NORMALIZE "${${type}_BASE}/${file}")
            list(APPEND SOURCES "$<BUILD_INTERFACE:${FILE_PATH}")
        endforeach()
    endforeach()


    if (__MODE STREQUAL "VERBATIM")
        list(TRANSFORM SOURCES APPEND ">")
    elseif (__MODE STREQUAL "SMART_HEADER_ONLY" OR __MODE STREQUAL "SMART_SOURCE")
        list(TRANSFORM SOURCES APPEND ".test.cpp>" OUTPUT_VARIABLE TEST_SOURCES)

        if (__MODE STREQUAL "SMART_SOURCE")
            list(TRANSFORM SOURCES APPEND ".cpp>" OUTPUT_VARIABLE SOURCES_CPP)
        else ()
            set(SOURCES_CPP)
        endif()
        list(TRANSFORM SOURCES APPEND ".hpp>")
        list(APPEND SOURCES ${SOURCES_CPP})
    else ()
        message(FATAL_ERROR "Unknown mode ${__MODE}")
    endif ()

    get_target_property(TARGET_TYPE "${TARGET}" TYPE)
    if (TARGET_TYPE STREQUAL "INTERFACE_LIBRARY")
        set(VISIBILITY INTERFACE)
    else ()
        set(VISIBILITY PRIVATE)
    endif ()

    target_sources("${TARGET}" ${VISIBILITY} ${SOURCES})
    if (DEFINED __TEST_TARGET AND NOT __MODE STREQUAL "VERBATIM")
        if (BUILD_TESTING)
            target_sources("${__TEST_TARGET}" PRIVATE ${TEST_SOURCES})
        endif ()
    elseif (NOT __MODE STREQUAL "VERBATIM")
        message(FATAL_ERROR "Specified smart mode without a test target.")
    endif ()
endfunction()
