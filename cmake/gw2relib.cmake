include_guard()
include(FetchContent)

FetchContent_Declare(
    gw2relib
    GIT_REPOSITORY https://github.com/TyrianDeveloperCollective/GW2RE-lib
    GIT_TAG 6a745bdda8187b51d436f32c7ea06744eda4cea5
)

FetchContent_MakeAvailable(gw2relib)

file(GLOB_RECURSE SOURCES
    "${gw2relib_SOURCE_DIR}/*.cpp"
    "${gw2relib_SOURCE_DIR}/*.h"
)

add_library(gw2relib STATIC)
target_sources(gw2relib PRIVATE ${SOURCES})
target_include_directories(gw2relib PUBLIC ${gw2relib_SOURCE_DIR})
