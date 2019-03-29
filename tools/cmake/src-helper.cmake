# Written in 2015 by Henrik Steffen Gaßmann <henrik@gassmann.onl>
#
# To the extent possible under law, the author(s) have dedicated all
# copyright and related and neighboring rights to this software to the
# public domain worldwide. This software is distributed without any warranty.
#
# You should have received a copy of the CC0 Public Domain Dedication
# along with this software. If not, see
#
#     http://creativecommons.org/publicdomain/zero/1.0/
#
########################################################################
# macro definitions

macro(DEF_SOURCE_GROUP TARGET GROUP_NAME)
    unset(DSG_SUFFIX)
    unset(_DSG_H)
    unset(_DSG_S)
    foreach(it ${ARGN})
        if(it STREQUAL "HEADERS")
            set(DSG_SUFFIX H)
        elseif(it STREQUAL "SOURCES")
            set(DSG_SUFFIX S)
        else()
            if (NOT DEFINED DSG_SUFFIX)
                MESSAGE(FATAL_ERROR "DEF_SOURCE_GROUP wrong usage")
            endif()
            list(APPEND "_DSG_${DSG_SUFFIX}" "${it}")
        endif()
    endforeach()
    unset(DSG_SUFFIX)

    target_sources("${TARGET}" PRIVATE ${_DSG_H})
    source_group("${GROUP_NAME}\\include" FILES ${_DSG_H})

    target_sources("${TARGET}" PRIVATE ${_DSG_S})
    source_group("${GROUP_NAME}\\src" FILES ${_DSG_S})

    unset(_DSG_H)
    unset(_DSG_S)

    set(${PROJECT_NAME}_${VAR_NAME} ${${PROJECT_NAME}_${VAR_NAME}_S} ${${PROJECT_NAME}_${VAR_NAME}_H})
    list(APPEND ${PROJECT_NAME}_SRC ${${PROJECT_NAME}_${VAR_NAME}})
    list(APPEND ${PROJECT_NAME}_GROUPS "${PROJECT_NAME}_${VAR_NAME}:${GROUP_NAME}")
endmacro()
