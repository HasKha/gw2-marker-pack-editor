# ── markus_pack ───────────────────────────────────────────────────────────────
# Everything that understands the marker pack format, and nothing that knows about Nexus,
# imgui or the game. Keeping this layer standalone is what lets markus_pack_test and markus_probe
# exercise it without launching GW2.
add_library(markus_pack STATIC)
markus_target(markus_pack)

target_include_directories(markus_pack PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(markus_pack PUBLIC
    markus_util
    glm::glm
    pugixml::pugixml
    miniz
    libdeflate::libdeflate_static
)

markus_layering(ALLOWS util)
