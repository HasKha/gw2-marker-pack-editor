# ── markus_viewer_test ────────────────────────────────────────────────────────
# The viewer's assertions, in a binary that links markus_viewer and not markus_editor: that it links
# at all is the proof the viewer stands without the editor. Also the corpus sweep, which
# checks the format and the bake in one pass over every pack.
add_executable(markus_viewer_test)
markus_target_sources(markus_viewer_test)

target_link_libraries(markus_viewer_test PRIVATE
    markus_viewer
    markus_corpus
    Catch2::Catch2WithMain
)

catch_discover_tests(markus_viewer_test)
