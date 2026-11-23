cmake_minimum_required(VERSION 3.31)
project(MarkerPackEditor)

# Keep in step with the release tag: Nexus compares the tag on GitHub against the version
# compiled in here, and only offers an update when the tag is greater. Tags are
# vMAJOR.MINOR.BUILD — its parser rejects a two-component version.
set(VERSION_MAJOR 1)
set(VERSION_MINOR 6)
set(VERSION_BUILD 0)

add_compile_definitions(
    "VERSION_MAJOR=${VERSION_MAJOR}"
    "VERSION_MINOR=${VERSION_MINOR}"
    "VERSION_BUILD=${VERSION_BUILD}"
    "NOMINMAX"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(imgui)
include(imguizmo)
include(mumble)
include(nexus)
include(json)
include(glm)
include(pugixml)
include(miniz)
include(nfd)
include(catch2)
include(good_render)
include(lua)
include(iconfontcppheaders)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin/")

# Every target is the folder it is in, defined by that folder's CMakeLists.txt; the table of
# them, and what links what, is docs/architecture.md#build-targets. Listed from the bottom of
# the layering up: each library links only those above it in this list.
include(mpe)

# enable_testing() rather than include(CTest): we want ctest, not the CDash submission
# targets and options that module also brings along. Before the folders, so the tests they
# define reach ctest.
enable_testing()
include(Catch)

add_subdirectory(src/util)
add_subdirectory(src/http)
add_subdirectory(src/pack)
add_subdirectory(src/viewer)
add_subdirectory(src/editor)
add_subdirectory(src/addon)

add_subdirectory(test/corpus)
add_subdirectory(test/probe)
add_subdirectory(test/http)
add_subdirectory(test/pack)
add_subdirectory(test/viewer)
add_subdirectory(test/editor)
