# ── Markus ────────────────────────────────────────────────────────────────────
# The Nexus addon itself: the entry point, and nothing else.
add_library(Markus SHARED)
markus_target(Markus)

target_link_libraries(Markus PRIVATE markus_editor)
