include_guard()
include(FetchContent)

FetchContent_Declare(
    mumble
    GIT_REPOSITORY https://github.com/RaidcoreGG/RCGG-lib-mumble-api.git
    GIT_TAG 8c93788c3042dd6b401c0a93d17e34e1d4414e9a
)

FetchContent_MakeAvailable(mumble)

file(GLOB_RECURSE SOURCES
    "${mumble_SOURCE_DIR}/*.cpp"
    "${mumble_SOURCE_DIR}/*.h"
)

add_library(mumble INTERFACE)
target_sources(mumble PRIVATE ${SOURCES})
target_include_directories(mumble INTERFACE ${mumble_SOURCE_DIR})
