# ── mpe_util ──────────────────────────────────────────────────────────────────
# What any part of the addon could use and none of it owns: paths as UTF-8 text, the small
# JSON files kept between sessions. Knows nothing of packs, the game or the addon, and
# includes nothing else of ours.
add_library(mpe_util STATIC)
mpe_target_sources(mpe_util)

target_include_directories(mpe_util PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(mpe_util PUBLIC nlohmann_json::nlohmann_json)

mpe_layering()
