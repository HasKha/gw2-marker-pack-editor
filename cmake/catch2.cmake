include_guard()
include(FetchContent)

# Test framework for mpe_test. Test-only: nothing in src/ links it, and the addon does
# not know it exists.
#
# v3 is the compiled-library generation rather than the old single header, so it comes
# in as a normal subproject. Its own CMakeLists asks for 3.16, comfortably above the 3.5
# floor CMake 4 enforces, so unlike miniz this one needs no amalgamation trick.
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)

# We only ever build the library, never Catch2's own test suite or its examples.
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_EXTRA_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Catch2)

# Puts catch_discover_tests() on the module path, so every TEST_CASE shows up to ctest
# as its own test rather than one opaque "run the exe" entry.
list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
