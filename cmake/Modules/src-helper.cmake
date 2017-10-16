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

macro(DEF_SOURCE_GROUP PROJECT_NAME VAR_NAME GROUP_NAME)
    unset(DSG_SUFFIX)
    foreach(it ${ARGN})
        if(it STREQUAL "HEADERS")
            set(DSG_SUFFIX H)
        elseif(it STREQUAL "SOURCES")
            set(DSG_SUFFIX S)
        else()
            if (NOT DEFINED DSG_SUFFIX)
                MESSAGE(FATAL_ERROR "DEF_SOURCE_GROUP wrong usage")
            endif()
            list(APPEND "${PROJECT_NAME}_${VAR_NAME}_${DSG_SUFFIX}" "${it}")
        endif()
    endforeach()
    unset(DSG_SUFFIX)

    set(${PROJECT_NAME}_${VAR_NAME} ${${PROJECT_NAME}_${VAR_NAME}_S} ${${PROJECT_NAME}_${VAR_NAME}_H})
    list(APPEND ${PROJECT_NAME}_SRC ${${PROJECT_NAME}_${VAR_NAME}})
    list(APPEND ${PROJECT_NAME}_GROUPS "${PROJECT_NAME}_${VAR_NAME}:${GROUP_NAME}")
endmacro()

macro(GROUP_FILES PROJECT_NAME)
    foreach(SRC_GROUP ${${PROJECT_NAME}_GROUPS})
        string(REPLACE ":" ";" SRC_GROUP_PAIR ${SRC_GROUP})
        list(GET SRC_GROUP_PAIR 0 VAR_NAME)
        list(GET SRC_GROUP_PAIR 1 GROUP_NAME)
        string(REPLACE "/" "\\\\" GROUP_NAME "${GROUP_NAME}")
        source_group("${GROUP_NAME}\\src" FILES ${${VAR_NAME}_S})
        source_group("${GROUP_NAME}\\include" FILES ${${VAR_NAME}_H})
    endforeach()
endmacro()
