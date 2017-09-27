
add_executable(vefs-tests
    vefs-tests.cpp
    boost-unit-test.hpp
)
target_link_libraries(vefs-tests
    PUBLIC
        vefs
        Boost::unit_test_framework
)
add_test(NAME vefs-tests COMMAND vefs-tests)
