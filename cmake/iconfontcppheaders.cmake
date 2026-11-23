include_guard(GLOBAL)
include(FetchContent)

FetchContent_Declare(
    iconfontcppheaders
    GIT_REPOSITORY https://github.com/juliettef/IconFontCppHeaders
    GIT_TAG 210b5a399a64270674560d633638952d1e8d804d
)

FetchContent_MakeAvailable(iconfontcppheaders)

add_library(iconfontcppheaders INTERFACE)
target_include_directories(iconfontcppheaders INTERFACE "${iconfontcppheaders_SOURCE_DIR}")
