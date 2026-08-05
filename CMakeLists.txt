cmake_minimum_required(VERSION 3.31)
project(MarkerPackEditor)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)

add_compile_definitions(
    "VERSION_MAJOR=${VERSION_MAJOR}"
    "VERSION_MINOR=${VERSION_MINOR}"
    "NOMINMAX"
)
# Deliberately NOT defining WIN32_LEAN_AND_MEAN: it drops winsock from <Windows.h>, and
# gw2relib's VdfContext.h uses IN_ADDR.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(imgui)
include(imguizmo)
include(mumble)
include(nexus)
include(gw2relib)
include(json)
include(glm)
include(pugixml)
include(miniz)
include(nfd)
include(catch2)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin/")

# ── mpe_pack ──────────────────────────────────────────────────────────────────
# Everything that understands the marker pack format, and nothing that knows about
# Nexus, imgui or the game. Keeping this layer standalone is what lets the console
# harness below exercise it without launching GW2.
add_library(mpe_pack STATIC)

file(GLOB_RECURSE PACK_SOURCES
    "src/pack/*.h"
    "src/pack/*.cpp"
)
target_sources(mpe_pack PRIVATE ${PACK_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "" FILES ${PACK_SOURCES})

target_include_directories(mpe_pack PUBLIC "src/")
target_link_libraries(mpe_pack PUBLIC
    glm::glm
    pugixml::pugixml
    miniz
)

# ── MarkerPackEditor ──────────────────────────────────────────────────────────
# The Nexus addon itself.
add_library(MarkerPackEditor SHARED)

file(GLOB_RECURSE SOURCES
    "src/*.h"
    "src/*.cpp"
)
list(REMOVE_ITEM SOURCES ${PACK_SOURCES})
target_sources(MarkerPackEditor PRIVATE ${SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "" FILES ${SOURCES})

target_include_directories(MarkerPackEditor PRIVATE "src/")
target_link_libraries(MarkerPackEditor PRIVATE
    mpe_pack
    imgui
    imguizmo
    mumble
    nexus
    gw2relib
    nlohmann_json::nlohmann_json
    nfd
    shell32  # SHGetKnownFolderPath
    ole32    # CoTaskMemFree
)

# ── Test corpus ───────────────────────────────────────────────────────────────
# Both test targets read real marker packs rather than fixtures: what a pack author
# actually ships is the thing worth being correct about, and no synthetic file exercises
# the reader the way 3,863 entries of hand-authored XML does. The corpus is not in the
# repo — these are other people's packs — so its location is configured here and can be
# overridden at run time with MPE_TEST_PACK_DIR / MPE_TEST_FOLDER_PACK.
set(MPE_TEST_PACK_DIR "$ENV{USERPROFILE}/Documents/Guild Wars 2/addons/blishhud/markers"
    CACHE PATH "Folder of .zip/.taco marker packs to test against")
set(MPE_TEST_FOLDER_PACK "$ENV{USERPROFILE}/Documents/git/gw2-markers"
    CACHE PATH "An unzipped pack, to check a folder source matches the zip built from it")

add_library(mpe_corpus STATIC)
target_sources(mpe_corpus PRIVATE "test/corpus.h" "test/corpus.cpp")
target_include_directories(mpe_corpus PUBLIC "test/")
target_compile_definitions(mpe_corpus PRIVATE
    "MPE_TEST_PACK_DIR_DEFAULT=\"${MPE_TEST_PACK_DIR}\""
    "MPE_TEST_FOLDER_PACK_DEFAULT=\"${MPE_TEST_FOLDER_PACK}\""
)

# ── mpe_probe ─────────────────────────────────────────────────────────────────
# Console harness: loads a pack and reports what it found, what it could not parse,
# and whether it survives a byte-exact round trip.
#
# It is the *diagnostic* half — it prints, it does not assert, and its output is how a
# format question gets answered. mpe_test below is the half that fails a build.
add_executable(mpe_probe)

set(PROBE_SOURCES "test/pack_probe.cpp")
target_sources(mpe_probe PRIVATE ${PROBE_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "" FILES ${PROBE_SOURCES})

target_link_libraries(mpe_probe PRIVATE mpe_pack mpe_corpus)

# ── mpe_test ──────────────────────────────────────────────────────────────────
# The assertions. Everything docs/plan.md and docs/architecture.md claim about the reader
# and the writer lives here as a check, so a regression fails rather than showing up as a
# different number in the probe's output.
# enable_testing() rather than include(CTest): we want ctest, not the CDash submission
# targets and options that module also brings along.
enable_testing()
add_executable(mpe_test)

file(GLOB TEST_SOURCES "test/*_test.cpp")
target_sources(mpe_test PRIVATE ${TEST_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "" FILES ${TEST_SOURCES})

target_link_libraries(mpe_test PRIVATE mpe_pack mpe_corpus Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(mpe_test)
