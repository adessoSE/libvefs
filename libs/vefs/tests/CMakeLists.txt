
add_executable(vefs-tests
    vefs-tests.cpp
    boost-unit-test.hpp
    test-utils.hpp

    disappointment-tests.cpp
    allocator-tests.cpp
    cache-tests.cpp

    osfilesystem-tests.cpp
    memfs.cpp
    memfs-tests.cpp
    memfs.hpp

    raw_archive-tests.cpp
    archive-tests.cpp

    blob-tests.cpp
    crypto_provider-tests.cpp
)
target_link_libraries(vefs-tests
    PUBLIC
        vefs
        Boost::unit_test_framework
)
add_test(NAME vefs-tests COMMAND vefs-tests)
