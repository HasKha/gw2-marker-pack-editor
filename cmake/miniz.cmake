include_guard()
include(FetchContent)

# Zip reading/writing. Marker packs are only loadable by Blish HUD when zipped, so we
# always read from and write back to a .zip (or .taco).
#
# We pull the release *amalgamation* rather than the git repo: the repo's CMakeLists
# declares cmake_minimum_required(VERSION 2.8), which CMake 4 refuses outright, and it
# generates a miniz_export.h we would otherwise have to reproduce. The amalgamation is
# a self-contained miniz.c/miniz.h pair with no build system at all.
FetchContent_Declare(
    miniz
    URL https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip
)

FetchContent_MakeAvailable(miniz)

add_library(miniz STATIC)
set(SOURCES
    "${miniz_SOURCE_DIR}/miniz.h"
    "${miniz_SOURCE_DIR}/miniz.c"
)
source_group(TREE "${miniz_SOURCE_DIR}" FILES ${SOURCES})
target_sources(miniz PRIVATE ${SOURCES})
target_include_directories(miniz PUBLIC "${miniz_SOURCE_DIR}")
