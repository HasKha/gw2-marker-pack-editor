include_guard()
include(FetchContent)
include(imgui)

# ImGuizmo ships a CMakeLists that only builds its example app, so we point
# SOURCE_SUBDIR at a path that does not exist to fetch-without-configure, then
# declare the library ourselves the same way imgui.cmake does.
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 1.83
    SOURCE_SUBDIR does-not-exist
)

FetchContent_MakeAvailable(imguizmo)

add_library(imguizmo)
set(SOURCES
    "${imguizmo_SOURCE_DIR}/ImGuizmo.h"
    "${imguizmo_SOURCE_DIR}/ImGuizmo.cpp"
)
source_group(TREE "${imguizmo_SOURCE_DIR}" FILES ${SOURCES})
target_sources(imguizmo PRIVATE ${SOURCES})
target_include_directories(imguizmo PUBLIC "${imguizmo_SOURCE_DIR}")
target_link_libraries(imguizmo PUBLIC imgui)
