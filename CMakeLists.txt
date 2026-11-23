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

# One directory under src/ per target, so a file's folder says which library it is in and
# no target has to be described by subtracting another's sources.
#
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

# ── mpe_viewer ────────────────────────────────────────────────────────────────
# What every player runs: the camera, textures, drawing packs into the frame. It links
# the format and never the editor — the dependency points one way, editor -> viewer, so a
# player who never turns editing on runs none of the editor's code. The ctest case
# viewer_includes_no_editor below holds the line at the source level.
add_library(mpe_viewer STATIC)

file(GLOB_RECURSE VIEWER_SOURCES
    "src/viewer/*.h"
    "src/viewer/*.cpp"
)
target_sources(mpe_viewer PRIVATE ${VIEWER_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "" FILES ${VIEWER_SOURCES})

target_include_directories(mpe_viewer PUBLIC "src/")
target_link_libraries(mpe_viewer PUBLIC
    mpe_pack
    imgui
    mumble
    nexus
    nlohmann_json::nlohmann_json
    good_render
    iconfontcppheaders
    sol2
    shell32  # SHGetKnownFolderPath
    ole32    # CoTaskMemFree
    bcrypt winhttp   # BCryptHash, for Pathing's MD5 of a made-up guid
)

# ── mpe_editor ────────────────────────────────────────────────────────────────
# The editor on top of the format: selection, undo, creating and deleting, the asset
# cache, the windows and the world view. A library rather than part of the DLL so the
# test binary links the same code the addon runs, instead of listing source files again.
add_library(mpe_editor STATIC)

file(GLOB_RECURSE EDITOR_SOURCES
    "src/editor/*.h"
    "src/editor/*.cpp"
)
target_sources(mpe_editor PRIVATE ${EDITOR_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "" FILES ${EDITOR_SOURCES})

target_include_directories(mpe_editor PUBLIC "src/")
target_link_libraries(mpe_editor PUBLIC
    mpe_pack
    mpe_viewer
    imgui
    imguizmo
    mumble
    nexus
    nlohmann_json::nlohmann_json
    nfd
)

# ── MarkerPackEditor ──────────────────────────────────────────────────────────
# The Nexus addon itself: the entry point, and nothing else.
add_library(MarkerPackEditor SHARED "src/entry.cpp")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "" FILES "src/entry.cpp")
target_link_libraries(MarkerPackEditor PRIVATE mpe_editor)

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

# mpe_editor pulls mpe_pack in with it. Only the objects the tests reach get linked, so
# nothing here needs a live Nexus or an imgui context; what does touch Nexus reaches it
# through an AddonAPI* that stays null, which resources.cpp already has to handle.
target_link_libraries(mpe_test PRIVATE
    mpe_editor
    mpe_corpus
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(mpe_test)

# ── mpe_viewer_test ───────────────────────────────────────────────────────────
# The viewer's assertions, in a binary that links mpe_viewer and not mpe_editor: that it
# links at all is the proof the viewer stands without the editor, and the case below is the
# proof nothing in its source reaches for one.
add_executable(mpe_viewer_test)

file(GLOB VIEWER_TEST_SOURCES "test/viewer/*_test.cpp" "test/viewer/*.h")
target_sources(mpe_viewer_test PRIVATE ${VIEWER_TEST_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "" FILES ${VIEWER_TEST_SOURCES})

target_link_libraries(mpe_viewer_test PRIVATE
    mpe_viewer
    mpe_corpus
    Catch2::Catch2WithMain
)

catch_discover_tests(mpe_viewer_test)

add_test(NAME viewer_includes_no_editor
    COMMAND ${CMAKE_COMMAND} "-DVIEWER_DIR=${PROJECT_SOURCE_DIR}/src/viewer" -P "${PROJECT_SOURCE_DIR}/cmake/check_viewer_includes.cmake")
