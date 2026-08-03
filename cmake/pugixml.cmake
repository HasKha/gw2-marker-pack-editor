include_guard()
include(FetchContent)

# XML parsing/writing. Keeps comments and unknown attributes as real nodes, which
# is what lets us round-trip a hand-authored pack without destroying it.
FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.15
)

set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(pugixml)
