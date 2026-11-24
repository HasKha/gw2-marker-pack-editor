include_guard()
include(FetchContent)

# The RealTime API's contract: RTAPI.hpp, and nothing to build. The addon itself is closed source;
# this repository is its releases and its headers. The camera is read from it when it is loaded
# and from MumbleLink when it is not, so this is a compile-time dependency only.
#
# Fetched with SOURCE_SUBDIR pointing nowhere: the repository has no CMakeLists of its own, and
# all we want is the header at its root.
FetchContent_Declare(
    rtapi
    GIT_REPOSITORY https://github.com/gwdevcommunity/GW2-RealTime-API-Releases.git
    GIT_TAG fca173b8b1f50809fea6702496197d1348807ae2
    SOURCE_SUBDIR no-cmake-here
)

FetchContent_MakeAvailable(rtapi)

add_library(rtapi INTERFACE)
target_include_directories(rtapi INTERFACE "${rtapi_SOURCE_DIR}")
