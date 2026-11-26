include_guard(GLOBAL)
include(FetchContent)

# stb_image, to decode a trail's texture on our side for the colour it is drawn with on the map
# (viewer/render/trail_colour.*). Nexus decodes the images it draws but hands no pixels back. stb_image
# rather than WIC because MonoGame, which decodes Pathing's, does it through StbImageSharp, a port
# of the same file: the same pixels come out.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 6b5799bae8a1fe3792691ef4b72ae26434c3e799
)

FetchContent_MakeAvailable(stb)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${stb_SOURCE_DIR}")
