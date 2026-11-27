/*
** Where Lua decides how a float is written and whether a float can be an integer, decided as
** NeoLua decides them under Pathing. cmake/lua.cmake patches the one place in Lua that makes
** each decision to call these, so every library, tostring and `..` agree, and the parity cases in
** test/viewer/lua_parity hold them to NeoLua.
*/
#ifndef MARKUS_NEOLUA_H
#define MARKUS_NEOLUA_H

#include <stddef.h>

#include "lua.h"

/*
** A float as .NET Framework's Double.ToString writes it, in the invariant culture: 15 significant
** digits, no ".0" on a whole number, an exponent from 1E+15 up and below 0.0001 ("1E+20"),
** "Infinity", "NaN", and -0 as "0". NeoLua's own floats are single precision, which writes 1/3 as
** 0.3333333; a double cannot know it would have been a single, so that much stays Lua's.
*/
int neolua_number2str(char* buff, size_t size, lua_Number n);

/*
** Argument `arg` where a library wants an integer and it is not one. NeoLua's binder hands a
** float to an int parameter as C#'s cast does, toward zero, so string.sub("abcdef", 1, 2.7) is
** "ab" and string.sub("abcdef", -2.5) is "ef", where Lua raises "number has no integer
** representation". 0 when it is no number, or out of range once cut.
*/
int neolua_tointeger(lua_State* L, int arg, lua_Integer* out);

#endif
