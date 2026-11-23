# ── mpe_corpus ────────────────────────────────────────────────────────────────
# Where the real marker packs are, for every target that reads them.
#
# The tests read real packs rather than fixtures: what a pack author actually ships is the
# thing worth being correct about, and no synthetic file exercises the reader the way 3,863
# entries of hand-authored XML does. The corpus is not in the repo — these are other people's
# packs — so its location is configured here and can be overridden at run time with
# MPE_TEST_PACK_DIR / MPE_TEST_FOLDER_PACK.
set(MPE_TEST_PACK_DIR "$ENV{USERPROFILE}/Documents/Guild Wars 2/addons/blishhud/markers"
    CACHE PATH "Folder of .zip/.taco marker packs to test against")
set(MPE_TEST_FOLDER_PACK "$ENV{USERPROFILE}/Documents/git/gw2-markers"
    CACHE PATH "An unzipped pack, to check a folder source matches the zip built from it")

add_library(mpe_corpus STATIC)
mpe_target_sources(mpe_corpus)

target_include_directories(mpe_corpus PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mpe_corpus PRIVATE
    "MPE_TEST_PACK_DIR_DEFAULT=\"${MPE_TEST_PACK_DIR}\""
    "MPE_TEST_FOLDER_PACK_DEFAULT=\"${MPE_TEST_FOLDER_PACK}\""
)
