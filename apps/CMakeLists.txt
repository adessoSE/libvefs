
add_executable(vefs-cli)
target_include_directories(vefs-cli
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

 target_sources(vefs-cli
    PRIVATE
        vefs.main.cpp

        vefs/cli/error.hpp
        vefs/cli/error.cpp
        vefs/cli/utils.hpp
        vefs/cli/utils.cpp

        vefs/cli/commandlets/base.cpp
        vefs/cli/commandlets/base.hpp
        vefs/cli/commandlets/extract.cpp
        vefs/cli/commandlets/extract.hpp
        vefs/cli/commandlets/extract_all.cpp
        vefs/cli/commandlets/extract_all.hpp
        vefs/cli/commandlets/extract_personalization.cpp
        vefs/cli/commandlets/extract_personalization.hpp
        vefs/cli/commandlets/upsert.cpp
        vefs/cli/commandlets/upsert.hpp
        vefs/cli/commandlets/validate.cpp
        vefs/cli/commandlets/validate.hpp

        vefs/cli/key-provider/mdc.cpp
        vefs/cli/key-provider/mdc.hpp
        vefs/cli/key-provider/raw.cpp
        vefs/cli/key-provider/raw.hpp
)

target_link_libraries(vefs-cli
    PUBLIC
        vefs
        bfg::lyra
        aklomp::base64
)

set_target_properties(vefs-cli PROPERTIES
    OUTPUT_NAME vefs
)

##########################################################################
# install targets
install(TARGETS vefs-cli EXPORT vefs-targets)
