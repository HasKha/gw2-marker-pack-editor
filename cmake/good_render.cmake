include_guard()
include(FetchContent)

# GoodRender's contract: one header, include/good_render.h, and nothing to build. The viewer
# draws through it when the service is loaded and falls back to a draw list when it is not, so
# this is a compile-time dependency only — no DLL of GoodRender's is ever linked or required.
#
# Fetched with SOURCE_SUBDIR pointing nowhere: the repository has no CMakeLists of its own, and
# all we want is its include folder.
FetchContent_Declare(
    good_render
    GIT_REPOSITORY https://github.com/HasKha/gw2-good-render.git
    GIT_TAG aa3e3c1b05cbc135fe1420f58e43c15bf0f81e23
    SOURCE_SUBDIR no-cmake-here
)

FetchContent_MakeAvailable(good_render)

add_library(good_render INTERFACE)
target_include_directories(good_render INTERFACE "${good_render_SOURCE_DIR}/include")
