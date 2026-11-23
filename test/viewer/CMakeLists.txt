# ── mpe_viewer_test ───────────────────────────────────────────────────────────
# The viewer's assertions, in a binary that links mpe_viewer and not mpe_editor: that it links
# at all is the proof the viewer stands without the editor. Also the corpus sweep, which
# checks the format and the bake in one pass over every pack.
add_executable(mpe_viewer_test)
mpe_target_sources(mpe_viewer_test)

target_link_libraries(mpe_viewer_test PRIVATE
    mpe_viewer
    mpe_corpus
    Catch2::Catch2WithMain
)

catch_discover_tests(mpe_viewer_test)
