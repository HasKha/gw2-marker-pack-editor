include_guard()
include(FetchContent)

FetchContent_Declare(
    nexus
    GIT_REPOSITORY https://github.com/RaidcoreGG/RCGG-lib-nexus-api
    GIT_TAG e585220d489ed27feeb2dd208845f15472e0cf04
)

FetchContent_MakeAvailable(nexus)

file(GLOB_RECURSE SOURCES
    "${nexus_SOURCE_DIR}/*.cpp"
    "${nexus_SOURCE_DIR}/*.h"
)

add_library(nexus INTERFACE)
target_sources(nexus PRIVATE ${SOURCES})
target_include_directories(nexus INTERFACE ${nexus_SOURCE_DIR})
