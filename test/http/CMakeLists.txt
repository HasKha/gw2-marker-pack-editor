# ── markus_http_test ──────────────────────────────────────────────────────────
# Requests that end when told to, and the one-job thread. Links markus_http and nothing else of
# ours, which is the proof it stands alone.
add_executable(markus_http_test)
markus_target_sources(markus_http_test)

target_link_libraries(markus_http_test PRIVATE
    markus_http
    Catch2::Catch2WithMain
    ws2_32  # the local server that never answers
)

catch_discover_tests(markus_http_test)
