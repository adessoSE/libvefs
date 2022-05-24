
function(enforce_out_of_source_builds)
  get_filename_component(REAL_SOURCE_DIR "${CMAKE_SOURCE_DIR}" REALPATH)
  get_filename_component(REAL_BINARY_DIR "${CMAKE_BINARY_DIR}" REALPATH)

  # disallow in-source builds
  if(REAL_SOURCE_DIR STREQUAL REAL_BINARY_DIR)
    message(FATAL_ERROR "\
    ######################################################
    # in-source builds are forbidden / unsupported.      #
    # Please create a distinct build directory.          #
    ######################################################")
  endif()
endfunction()

enforce_out_of_source_builds()
