# ── markus_probe ──────────────────────────────────────────────────────────────
# Console harness: loads a pack and reports what it found, what it could not parse, and
# whether it survives a byte-exact round trip.
#
# It is the *diagnostic* half — it prints, it does not assert, and its output is how a format
# question gets answered. The test binaries are the half that fails a build.
add_executable(markus_probe)
markus_target(markus_probe)

target_link_libraries(markus_probe PRIVATE markus_pack markus_corpus)
