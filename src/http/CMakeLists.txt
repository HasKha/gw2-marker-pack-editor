# ── markus_http ───────────────────────────────────────────────────────────────
# Requests to the web from a worker thread, through WinHTTP, and the one-job thread they run
# in. A library of its own so nothing about the web is mixed into what draws packs, and so it
# is tested on its own: markus_http_test links it and nothing else of ours.
add_library(markus_http STATIC)
markus_target(markus_http)

target_include_directories(markus_http PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(markus_http
    PUBLIC markus_util
    PRIVATE winhttp
)

markus_layering(ALLOWS util)
