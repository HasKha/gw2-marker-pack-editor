# ── markus_viewer ─────────────────────────────────────────────────────────────
# What every player runs: the camera, textures, drawing packs into the frame. It links the
# format and never the editor — the dependency points one way, editor -> viewer, so a player
# who never turns editing on runs none of the editor's code. markus_viewer_test linking without
# the editor holds that at the link, viewer_layering in the source.
add_library(markus_viewer STATIC)
markus_target(markus_viewer)

target_include_directories(markus_viewer PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(markus_viewer PUBLIC
    markus_util
    markus_http
    markus_pack
    imgui
    mumble
    nexus
    nlohmann_json::nlohmann_json
    good_render
    rtapi
    iconfontcppheaders
    stb
    sol2
    shell32  # SHGetKnownFolderPath
    ole32    # CoTaskMemFree
    bcrypt   # BCryptHash, for Pathing's MD5 of a made-up guid
)

markus_layering(ALLOWS util http pack)
