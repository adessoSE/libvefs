
add_executable(vefs-cli
    src/vefs-cli.cpp
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
install(TARGETS vefs-cli EXPORT vefs-targets
    RUNTIME DESTINATION bin/$<CONFIG>
    LIBRARY DESTINATION lib/$<CONFIG>
    ARCHIVE DESTINATION lib/$<CONFIG>
    INCLUDES DESTINATION include
)
