# ── markus_pack_test ──────────────────────────────────────────────────────────
# The format's assertions: attributes, the model, saving. Links markus_pack and not the viewer,
# which is the proof it can. The sweep over every corpus pack is in markus_viewer_test, because
# the same pass checks the viewer's bake.
add_executable(markus_pack_test)
markus_target_sources(markus_pack_test)

target_link_libraries(markus_pack_test PRIVATE
    markus_pack
    markus_corpus
    Catch2::Catch2WithMain
)

catch_discover_tests(markus_pack_test)
