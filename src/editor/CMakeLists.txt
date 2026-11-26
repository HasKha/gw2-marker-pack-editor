# ── markus_editor ─────────────────────────────────────────────────────────────
# The editor on top of the format: selection, undo, creating and deleting, the asset cache,
# the windows and the world view. A library rather than part of the DLL so markus_editor_test
# links the same code the addon runs, instead of listing source files again.
add_library(markus_editor STATIC)
markus_target(markus_editor)

target_include_directories(markus_editor PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(markus_editor PUBLIC
    markus_util
    markus_pack
    markus_viewer
    imgui
    imguizmo
    mumble
    nexus
    nlohmann_json::nlohmann_json
    nfd
)

markus_layering(ALLOWS util http pack viewer)
