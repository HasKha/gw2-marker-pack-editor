# ── mpe_http ──────────────────────────────────────────────────────────────────
# Requests to the web from a worker thread, through WinHTTP, and the one-job thread they run
# in. A library of its own so nothing about the web is mixed into what draws packs, and so it
# is tested on its own: mpe_http_test links it and nothing else of ours.
add_library(mpe_http STATIC)
mpe_target_sources(mpe_http)

target_include_directories(mpe_http PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(mpe_http
    PUBLIC mpe_util
    PRIVATE winhttp
)

mpe_layering(ALLOWS util)
