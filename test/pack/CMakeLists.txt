# ── mpe_pack_test ─────────────────────────────────────────────────────────────
# The format's assertions: attributes, the model, saving. Links mpe_pack and not the viewer,
# which is the proof it can. The sweep over every corpus pack is in mpe_viewer_test, because
# the same pass checks the viewer's bake.
add_executable(mpe_pack_test)
mpe_target_sources(mpe_pack_test)

target_link_libraries(mpe_pack_test PRIVATE
    mpe_pack
    mpe_corpus
    Catch2::Catch2WithMain
)

catch_discover_tests(mpe_pack_test)
