# ── markus_corpus ─────────────────────────────────────────────────────────────
# Where the real marker packs are, for every target that reads them.
#
# The tests read real packs rather than fixtures: what a pack author actually ships is the
# thing worth being correct about, and no synthetic file exercises the reader the way 3,863
# entries of hand-authored XML does. The corpus is not in the repo — these are other people's
# packs — so its location is configured here and can be overridden at run time with
# MARKUS_TEST_PACK_DIR / MARKUS_TEST_FOLDER_PACK.
set(MARKUS_TEST_PACK_DIR "$ENV{USERPROFILE}/Documents/Guild Wars 2/addons/blishhud/markers"
    CACHE PATH "Folder of .zip/.taco marker packs to test against")
set(MARKUS_TEST_FOLDER_PACK "$ENV{USERPROFILE}/Documents/git/gw2-markers"
    CACHE PATH "An unzipped pack, to check a folder source matches the zip built from it")

add_library(markus_corpus STATIC)
markus_target_sources(markus_corpus)

target_include_directories(markus_corpus PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(markus_corpus PRIVATE
    "MARKUS_TEST_PACK_DIR_DEFAULT=\"${MARKUS_TEST_PACK_DIR}\""
    "MARKUS_TEST_FOLDER_PACK_DEFAULT=\"${MARKUS_TEST_FOLDER_PACK}\""
)
