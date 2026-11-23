# ── mpe_editor_test ───────────────────────────────────────────────────────────
# The editor's assertions: the selection rules, trails, the lifecycle.
#
# Only the objects the tests reach get linked, so nothing here needs a live Nexus or an imgui
# context; what does touch Nexus reaches it through an AddonAPI* that stays null, which
# resources.cpp already has to handle.
add_executable(mpe_editor_test)
mpe_target_sources(mpe_editor_test)

target_link_libraries(mpe_editor_test PRIVATE
    mpe_editor
    mpe_corpus
    Catch2::Catch2WithMain
)

catch_discover_tests(mpe_editor_test)
