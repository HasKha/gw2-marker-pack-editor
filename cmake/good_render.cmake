include_guard()
include(FetchContent)

# GoodRender's contract: one header, include/good_render.h, and nothing to build. The viewer
# draws through it when the service is loaded and falls back to a draw list when it is not, so
# this is a compile-time dependency only — no DLL of GoodRender's is ever linked or required.
#
# Fetched with SOURCE_SUBDIR pointing nowhere: the repository has no CMakeLists of its own, and
# all we want is its include folder.

# Build against a local checkout of the public repo, `../gw2-good-render`, instead of the pin,
# while the contract changes and before it is pushed. A switch in this file rather than a -D,
# which the cache would keep through every later configure: here it shows in the diff, and
# configure warns while it is on. Off, with the pin moved to the pushed commit, once it is.
set(GOOD_RENDER_LOCAL OFF)
get_filename_component(GOOD_RENDER_LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../gw2-good-render" ABSOLUTE)

# FetchContent takes FETCHCONTENT_SOURCE_DIR_GOOD_RENDER over GIT_TAG, and caches it: only the
# switch sets it, as a plain variable, and one in the cache is dropped.
unset(FETCHCONTENT_SOURCE_DIR_GOOD_RENDER CACHE)
if(GOOD_RENDER_LOCAL)
    if(NOT EXISTS "${GOOD_RENDER_LOCAL_DIR}/include/good_render.h")
        message(FATAL_ERROR "good_render: GOOD_RENDER_LOCAL is on, but ${GOOD_RENDER_LOCAL_DIR} has no include/good_render.h")
    endif()
    message(WARNING "good_render: building against the local checkout ${GOOD_RENDER_LOCAL_DIR}, not the pin. "
                    "Turn GOOD_RENDER_LOCAL off before committing.")
    set(FETCHCONTENT_SOURCE_DIR_GOOD_RENDER "${GOOD_RENDER_LOCAL_DIR}")
else()
    unset(FETCHCONTENT_SOURCE_DIR_GOOD_RENDER)
endif()

FetchContent_Declare(
    good_render
    GIT_REPOSITORY https://github.com/HasKha/gw2-good-render.git
    GIT_TAG 2d5166434c919ce6dfc5f877f3955d841898db74
    SOURCE_SUBDIR no-cmake-here
)

FetchContent_MakeAvailable(good_render)

add_library(good_render INTERFACE)
target_include_directories(good_render INTERFACE "${good_render_SOURCE_DIR}/include")
