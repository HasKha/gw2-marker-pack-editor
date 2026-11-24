include_guard()
include(FetchContent)

# Test framework for the test binaries. Test-only: nothing in src/ links it.
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)

set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_EXTRA_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Catch2)

# Puts catch_discover_tests() on the module path, so every TEST_CASE shows up to ctest
# as its own test rather than one opaque "run the exe" entry.
list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
