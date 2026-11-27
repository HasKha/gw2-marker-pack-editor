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

# Two decisions made as NeoLua makes them, each in the one place Lua makes it (cmake/lua/neolua.h):
# how a float is written, in lobject.c, and whether a float passes where a library wants an
# integer, in lauxlib.c. Those two are compiled from a copy with the call put in; a Lua whose text
# has moved fails here rather than building without it.
set(LUA_NEOLUA_DIR "${CMAKE_CURRENT_LIST_DIR}/lua")
set(LUA_PATCHED_DIR "${CMAKE_CURRENT_BINARY_DIR}/lua_neolua")

# Replaces `from` with `to` in the copy of `file`, made from Lua's own on its first replacement.
# One replacement a call: a list of them would split at the semicolons of the C.
function(lua_patch file from to)
    if("${file}" IN_LIST LUA_PATCHED)
        set(text "${LUA_PATCHED_TEXT_${file}}")
    else()
        file(READ "${lua_SOURCE_DIR}/src/${file}" text)
        set(LUA_PATCHED ${LUA_PATCHED} "${file}" PARENT_SCOPE)
    endif()
    string(FIND "${text}" "${from}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "lua_patch: ${file} no longer has:\n${from}")
    endif()
    string(REPLACE "${from}" "${to}" text "${text}")
    set(LUA_PATCHED_TEXT_${file} "${text}" PARENT_SCOPE)
endfunction()

lua_patch(lobject.c [=[#include "lobject.h"]=] [=[#include "lobject.h"
#include "neolua.h"]=])
lua_patch(lobject.c
    [=[    len = lua_number2str(buff, MAXNUMBER2STR, fltvalue(obj));
    if (buff[strspn(buff, "-0123456789")] == '\0') {  /* looks like an int? */
      buff[len++] = lua_getlocaledecpoint();
      buff[len++] = '0';  /* adds '.0' to result */
    }]=]
    [=[    len = neolua_number2str(buff, MAXNUMBER2STR, fltvalue(obj));]=])
lua_patch(lauxlib.c [=[#include "lauxlib.h"]=] [=[#include "lauxlib.h"
#include "neolua.h"]=])
lua_patch(lauxlib.c
    [=[  if (l_unlikely(!isnum)) {
    interror(L, arg);
  }]=]
    [=[  if (l_unlikely(!isnum) && !neolua_tointeger(L, arg, &d)) {
    interror(L, arg);
  }]=])

# Written only when changed, so that configuring again does not rebuild Lua.
foreach(file IN LISTS LUA_PATCHED)
    file(WRITE "${LUA_PATCHED_DIR}/${file}.new" "${LUA_PATCHED_TEXT_${file}}")
    configure_file("${LUA_PATCHED_DIR}/${file}.new" "${LUA_PATCHED_DIR}/${file}" COPYONLY)
    list(REMOVE_ITEM LUA_SOURCES "${lua_SOURCE_DIR}/src/${file}")
    list(APPEND LUA_SOURCES "${LUA_PATCHED_DIR}/${file}")
endforeach()

add_library(lua STATIC ${LUA_SOURCES} "${LUA_NEOLUA_DIR}/neolua.c")
target_include_directories(lua PUBLIC "${lua_SOURCE_DIR}/src" PRIVATE "${LUA_NEOLUA_DIR}")
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
