# Written in 2019 by Henrik Steffen Gaßmann <henrik@gassmann.onl>
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

set(DOCS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")

add_custom_command(
    OUTPUT "${DOCS_OUTPUT_DIR}/index.html"
    COMMAND
        "${SPHINX_EXECUTABLE}" -b html
        "${CMAKE_CURRENT_LIST_DIR}" # sphinx input dir
        "${DOCS_OUTPUT_DIR}"  # sphinx output dir

    MAIN_DEPENDENCY "${CMAKE_CURRENT_LIST_DIR}/conf.py"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/index.rst"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Compiling project documentation"
)

add_custom_target(docs ALL DEPENDS "${DOCS_OUTPUT_DIR}/index.html")

install(DIRECTORY
    "${DOCS_OUTPUT_DIR}/"

    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
