include_guard()
include(FetchContent)
include(imgui)

FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 1.10
    SOURCE_SUBDIR does-not-exist
)

FetchContent_MakeAvailable(imguizmo)

# ImGuizmo 1.10 does ship a usable CMakeLists, but we cannot use it. It builds all six
# widgets in the repo as one target, and four of them (GraphEditor, ImCurveEdit,
# ImGradient, ImSequencer) need imgui 1.90+ for ImGuiChildFlags_FrameStyle and
# ImDrawFlags_RoundCornersAll. Nexus ships imgui 1.80. We only want the gizmo, so we
# declare a target over the one file we need and skip the rest.
# (Its CMakeLists also only links imgui when building its own example, so we would have
# had to attach imgui ourselves regardless.)
add_library(imguizmo)
set(SOURCES
    "${imguizmo_SOURCE_DIR}/src/ImGuizmo.h"
    "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp"
)
source_group(TREE "${imguizmo_SOURCE_DIR}" FILES ${SOURCES})
target_sources(imguizmo PRIVATE ${SOURCES})
target_include_directories(imguizmo PUBLIC "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(imguizmo PUBLIC imgui)

# The one thing ImGuizmo.cpp itself needs from a newer imgui. Up to 1.81 the last
# parameter of ImDrawList::AddPolyline was `bool closed`; 1.82 replaced it with
# ImDrawFlags, where ImDrawFlags_Closed is 1 — the same value `true` converts to. So
# defining it keeps the calls both compiling and correct.
#
# If imgui is ever upgraded past 1.82 this macro will collide with the real enumerator
# and fail loudly at compile time. That is the intended signal to delete this line.
target_compile_definitions(imguizmo PRIVATE ImDrawFlags_Closed=1)
