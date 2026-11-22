include_guard()
include(FetchContent)

# Native file dialogs. Provides the `nfd` target.
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG v1.3.0
)

set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nfd)

# nfd_win.cpp otherwise calls IFileDialog::SetDefaultFolder, which Windows ignores once it
# has a recently used location for the process — and the process is Gw2-64.exe, so that is
# shared with every other addon and can even be an Explorer search result. This switches it
# to SetFolder, which is unconditional.
target_compile_definitions(nfd PRIVATE NFD_OVERRIDE_RECENT_WITH_DEFAULT)
