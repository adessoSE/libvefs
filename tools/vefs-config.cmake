
include(CMakeFindDependencyMacro)

find_dependency(outcome CONFIG)
find_dependency(llfio CONFIG)

find_dependency(Boost COMPONENTS
    system
)

find_dependency(libb2 CONFIG)

find_dependency(boringssl CONFIG)

find_dependency(deeppack CONFIG)

find_dependency(fmt CONFIG)

include("${CMAKE_CURRENT_LIST_DIR}/vefs-targets.cmake")
