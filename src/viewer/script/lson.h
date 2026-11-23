#pragma once

#include <sol/sol.hpp>

#include <string>
#include <string_view>

// NeoLua's Lua Script Object Notation: a table written as a Lua table constructor of plain
// values. Pathing's `table` is NeoLua's LuaTable, so scripts have table.ToLson and
// table.FromLson, and Teh's Trails keeps its settings in Storage as LSON text — which is
// also what a Pathing install's kv.json holds for it. Written and read the way NeoLua does
// (LuaTable.ToLsonCore, LuaTable.FromLson, Lua.RtWriteValue / RtReadValue), so that text
// written by either reads back in the other.
namespace viewer::script {

// LuaTable.ToLson(table, prettyFormatting, indent). Raises for a value LSON has no notation
// for — a function, a marker — and for tables nested past a hundred levels.
//
// The array part comes first, as NeoLua lists it; the rest in whatever order Lua's `next`
// gives, where NeoLua keeps insertion order. Reading the text back does not care.
std::string toLson(lua_State* L, const sol::table& table, bool pretty, const std::string& indent);

// LuaTable.FromLson. Raises on anything that is not a table of plain values.
sol::table fromLson(sol::state_view lua, std::string_view text);

} // namespace viewer::script
