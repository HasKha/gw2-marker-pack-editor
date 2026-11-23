# ── MarkerPackEditor ──────────────────────────────────────────────────────────
# The Nexus addon itself: the entry point, and nothing else.
add_library(MarkerPackEditor SHARED)
mpe_target_sources(MarkerPackEditor)

target_link_libraries(MarkerPackEditor PRIVATE mpe_editor)
