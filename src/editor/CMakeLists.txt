# ── mpe_editor ────────────────────────────────────────────────────────────────
# The editor on top of the format: selection, undo, creating and deleting, the asset cache,
# the windows and the world view. A library rather than part of the DLL so mpe_editor_test
# links the same code the addon runs, instead of listing source files again.
add_library(mpe_editor STATIC)
mpe_target_sources(mpe_editor)

target_include_directories(mpe_editor PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(mpe_editor PUBLIC
    mpe_util
    mpe_pack
    mpe_viewer
    imgui
    imguizmo
    mumble
    nexus
    nlohmann_json::nlohmann_json
    nfd
)

mpe_layering(ALLOWS util http pack viewer)
