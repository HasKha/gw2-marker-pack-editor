#pragma once

#include <sol/sol.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

// How a Pathing object looks from Lua: a userdata whose members are rows in a table, one row
// per Lua-visible member, each with its getter, setter or function. Nothing about a member is
// written twice, and three things hold for every row because they are done here, once:
//
// - **Both call styles work.** NeoLua binds a method to its object, so `World:CategoryByType(x)`
//   and `World.CategoryByType(x)` do the same, and packs use both — Teh's Trails has both
//   spellings of several members. A method here comes back from indexing as a closure over
//   its object, and drops a leading argument that *is* that object.
// - **The census.** A member with no row is recorded, with the script's file and line, and
//   reads as nil; a write to one is recorded and dropped. See docs/viewer.md#the-census.
// - **Errors are Lua errors.** A row throws; sol2 turns that into a Lua error at the call, which
//   the protected call around every script entry reports to the console.
//
// Only the engine and the API tables include this; it is sol2 all the way down.
namespace viewer::script {

struct Context;

// The engine's state for the lua_State `L` belongs to, kept in its extra space.
Context& context(lua_State* L);
void setContext(lua_State* L, Context* context);

// The innermost Lua function on the stack: its chunk, as "<pack>/<file>", and line.
struct Location {
    std::string source;
    int line = 0;
};
Location callerLocation(lua_State* L);

// A binding's error, prefixed with where the script called it from, as Lua's own errors are.
[[noreturn]] void raise(lua_State* L, std::string_view message);

template <class T>
struct Member {
    const char* name = nullptr;
    sol::object (*get)(sol::state_view lua, T& self) = nullptr;
    void (*set)(sol::state_view lua, T& self, const sol::object& value) = nullptr;
    // A method: `args` are its arguments without the object, however it was called.
    sol::object (*call)(sol::state_view lua, T& self, sol::variadic_args args) = nullptr;
};

// Every bound type T provides
//     static constexpr const char* kName;                 // Pathing's name for it
//     static std::span<const Member<T>> members();
// and optionally
//     static std::string toString(const T&);              // what .NET's ToString gives
//     static void extend(sol::usertype<T>&);              // operators, for value types

namespace detail {

template <class T>
const Member<T>* findMember(std::string_view name) {
    static const std::unordered_map<std::string_view, const Member<T>*> by_name = [] {
        std::unordered_map<std::string_view, const Member<T>*> map;
        for (const Member<T>& member : T::members()) map.emplace(member.name, &member);
        return map;
    }();
    const auto it = by_name.find(name);
    return it == by_name.end() ? nullptr : it->second;
}

void recordMissing(lua_State* L, const char* type, const sol::object& key);

template <class T>
sol::object boundMethod(sol::state_view lua, const sol::object& self, const Member<T>* member) {
    // as_function: a lambda is otherwise pushed as a userdata of its own type.
    return sol::make_object(lua, sol::as_function([self, member](sol::this_state state, sol::variadic_args args) -> sol::object {
                                lua_State* L = state;
                                sol::variadic_args own = args;
                                if (args.size() > 0) {
                                    self.push(L);
                                    const bool called_with_colon = lua_rawequal(L, args.stack_index(), -1) != 0;
                                    lua_pop(L, 1);
                                    if (called_with_colon) own = sol::variadic_args(L, args.stack_index() + 1);
                                }
                                return member->call(sol::state_view(L), self.as<T&>(), own);
                            }));
}

template <class T>
concept HasToString = requires(const T& value) {
    { T::toString(value) } -> std::convertible_to<std::string>;
};

template <class T>
concept HasExtend = requires(sol::usertype<T>& type) { T::extend(type); };

} // namespace detail

template <class T>
void bindType(sol::state_view lua) {
    sol::usertype<T> type = lua.new_usertype<T>(
        T::kName,
        sol::no_constructor,
        sol::meta_function::index,
        [](sol::this_state state, const sol::object& self, const sol::object& key) -> sol::object {
            sol::state_view lua(state);
            if (key.get_type() == sol::type::string) {
                if (const Member<T>* member = detail::findMember<T>(key.as<std::string_view>())) {
                    if (member->get) return member->get(lua, self.as<T&>());
                    if (member->call) return detail::boundMethod(lua, self, member);
                }
            }
            detail::recordMissing(state, T::kName, key);
            return sol::lua_nil;
        },
        sol::meta_function::new_index,
        [](sol::this_state state, T& self, const sol::object& key, const sol::object& value) {
            if (key.get_type() == sol::type::string) {
                if (const Member<T>* member = detail::findMember<T>(key.as<std::string_view>())) {
                    if (!member->set) raise(state, std::string(T::kName) + "." + member->name + " cannot be set");
                    member->set(sol::state_view(state), self, value);
                    return;
                }
            }
            detail::recordMissing(state, T::kName, key);
        },
        sol::meta_function::to_string,
        [](const T& self) -> std::string {
            if constexpr (detail::HasToString<T>) {
                return T::toString(self);
            } else {
                return T::kName;
            }
        });
    if constexpr (detail::HasExtend<T>) T::extend(type);
}

// Argument `index` of `args`, counting from 0, or nil when there are fewer. Always tied to the
// state: a sol::object made from sol::lua_nil has none, and inspecting one crashes.
sol::object argument(const sol::variadic_args& args, size_t index);

// Arguments, read the way NeoLua converts them for a .NET parameter: a number for a number,
// and so on. `index` counts from 0 within `args`; a missing argument is nil.
//
// **nil is the .NET default** for a value type — 0, false, a zero vector, transparent black —
// as Lua.RtConvertValue makes it, rather than an error. Packs rely on it: Teh's Trails sets
// TrailSampleColor to a colour that is nil for its "Default" entry.
float argFloat(lua_State* L, const sol::variadic_args& args, size_t index, const char* what);
int argInt(lua_State* L, const sol::variadic_args& args, size_t index, const char* what);
bool argBool(const sol::variadic_args& args, size_t index); // Lua truthiness
std::string argString(lua_State* L, const sol::variadic_args& args, size_t index, const char* what); // numbers become text
bool argIsNil(const sol::variadic_args& args, size_t index);

// Lua 5.4's integer subtype: what NeoLua's Int32 is, as against a float.
bool isInteger(lua_State* L, const sol::object& value);

// A value assigned to a property, converted likewise.
float valueFloat(lua_State* L, const sol::object& value, const char* what);
std::string valueString(lua_State* L, const sol::object& value);

} // namespace viewer::script
