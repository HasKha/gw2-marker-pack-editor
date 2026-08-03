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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin/")

add_library(MarkerPackEditor SHARED)

file(GLOB_RECURSE SOURCES
    "src/*.h"
    "src/*.cpp"
)
target_sources(MarkerPackEditor PRIVATE ${SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "" FILES ${SOURCES})

target_include_directories(MarkerPackEditor PRIVATE "src/")
target_link_libraries(MarkerPackEditor PRIVATE
    imgui
    imguizmo
    mumble
    nexus
    gw2relib
    nlohmann_json::nlohmann_json
    glm::glm
    pugixml::pugixml
    miniz
    nfd
    shell32  # SHGetKnownFolderPath
    ole32    # CoTaskMemFree
)
