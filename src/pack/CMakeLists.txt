# ── mpe_pack ──────────────────────────────────────────────────────────────────
# Everything that understands the marker pack format, and nothing that knows about Nexus,
# imgui or the game. Keeping this layer standalone is what lets mpe_pack_test and mpe_probe
# exercise it without launching GW2.
add_library(mpe_pack STATIC)
mpe_target_sources(mpe_pack)

target_include_directories(mpe_pack PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(mpe_pack PUBLIC
    mpe_util
    glm::glm
    pugixml::pugixml
    miniz
)

mpe_layering(ALLOWS util)
