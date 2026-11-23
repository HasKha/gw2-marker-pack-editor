# ── mpe_viewer ────────────────────────────────────────────────────────────────
# What every player runs: the camera, textures, drawing packs into the frame. It links the
# format and never the editor — the dependency points one way, editor -> viewer, so a player
# who never turns editing on runs none of the editor's code. mpe_viewer_test linking without
# the editor holds that at the link, viewer_layering in the source.
add_library(mpe_viewer STATIC)
mpe_target_sources(mpe_viewer)

target_include_directories(mpe_viewer PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(mpe_viewer PUBLIC
    mpe_util
    mpe_http
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
    bcrypt   # BCryptHash, for Pathing's MD5 of a made-up guid
)

mpe_layering(ALLOWS util http pack)
