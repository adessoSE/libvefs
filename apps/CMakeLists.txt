
add_executable(vefs-cli
    vefs.main.cpp
)

target_link_libraries(vefs-cli
    PUBLIC
        vefs
        Boost::program_options
)

set_target_properties(vefs-cli PROPERTIES
    OUTPUT_NAME vefs
)

##########################################################################
# install targets
install(TARGETS vefs-cli EXPORT vefs-targets)
