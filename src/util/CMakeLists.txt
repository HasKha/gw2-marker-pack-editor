# ── markus_util ───────────────────────────────────────────────────────────────
# What any part of the addon could use and none of it owns: paths as UTF-8 text, the small
# JSON files kept between sessions. Knows nothing of packs, the game or the addon, and
# includes nothing else of ours.
add_library(markus_util STATIC)
markus_target_sources(markus_util)

target_include_directories(markus_util PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(markus_util PUBLIC nlohmann_json::nlohmann_json)

markus_layering()
