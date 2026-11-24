# ── markus_editor_test ────────────────────────────────────────────────────────
# The editor's assertions: the selection rules, trails, the lifecycle.
#
# Only the objects the tests reach get linked, so nothing here needs a live Nexus or an imgui
# context; what does touch Nexus reaches it through an AddonAPI* that stays null, which
# resources.cpp already has to handle.
add_executable(markus_editor_test)
markus_target_sources(markus_editor_test)

target_link_libraries(markus_editor_test PRIVATE
    markus_editor
    markus_corpus
    Catch2::Catch2WithMain
)

catch_discover_tests(markus_editor_test)
