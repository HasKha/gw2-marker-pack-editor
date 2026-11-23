include_guard()
include(FetchContent)

# Lua 5.4, for packs' scripts. 5.4 because its integer subtype is the nearest thing to what
# Pathing's NeoLua does with Int32 (docs/viewer.md#lua). Lua ships no CMake of its own, so
# the library is built here from its sources: everything in src/ but the two executables.
#
# Compiled as C, so a Lua error is a longjmp. sol2 is told so below and protects every call
# into Lua itself; nothing of ours sits between a lua_error and its pcall holding a destructor.
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
)
FetchContent_MakeAvailable(lua)

file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM LUA_SOURCES "${lua_SOURCE_DIR}/src/lua.c" "${lua_SOURCE_DIR}/src/luac.c")
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC "${lua_SOURCE_DIR}/src")
set_target_properties(lua PROPERTIES LINKER_LANGUAGE C)

# sol2, header-only: the C++ side of every binding. Its safeties are on, so a wrong type from
# a script is a Lua error the script console reports, never undefined behaviour.
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG v3.5.0
    SOURCE_SUBDIR no-cmake-here
)
FetchContent_MakeAvailable(sol2)

add_library(sol2 INTERFACE)
target_include_directories(sol2 SYSTEM INTERFACE "${sol2_SOURCE_DIR}/include")
target_compile_definitions(sol2 INTERFACE SOL_ALL_SAFETIES_ON=1 SOL_PRINT_ERRORS=0)
target_link_libraries(sol2 INTERFACE lua)
# A file of bindings is thousands of template instantiations, past COFF's default section limit.
target_compile_options(sol2 INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
