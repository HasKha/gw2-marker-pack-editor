# ── mpe_http_test ─────────────────────────────────────────────────────────────
# Requests that end when told to, and the one-job thread. Links mpe_http and nothing else of
# ours, which is the proof it stands alone.
add_executable(mpe_http_test)
mpe_target_sources(mpe_http_test)

target_link_libraries(mpe_http_test PRIVATE
    mpe_http
    Catch2::Catch2WithMain
    ws2_32  # the local server that never answers
)

catch_discover_tests(mpe_http_test)
