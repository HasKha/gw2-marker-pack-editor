#include "viewer/script/object.h"

#include <cmath>
#include <optional>
#include <stdexcept>

#include "viewer/script/context.h"

namespace viewer::script {

Context& context(lua_State* L) {
    return **static_cast<Context**>(lua_getextraspace(L));
}

void setContext(lua_State* L, Context* context) {
    *static_cast<Context**>(lua_getextraspace(L)) = context;
}

Location callerLocation(lua_State* L) {
    lua_Debug frame{};
    for (int level = 0; lua_getstack(L, level, &frame); ++level) {
        if (!lua_getinfo(L, "Sl", &frame) || frame.currentline < 0) continue;
        std::string_view source = frame.source ? frame.source : "";
        if (source.starts_with('@')) source.remove_prefix(1);
        return {std::string(source), frame.currentline};
    }
    return {"system", 0};
}

void raise(lua_State* L, std::string_view message) {
    const Location where = callerLocation(L);
    // Caught by sol2 at the boundary, and raised as a Lua error once nothing of ours is left
    // on the stack to unwind.
    throw std::runtime_error(where.source + ":" + std::to_string(where.line) + ": " + std::string(message));
}

namespace detail {

void recordMissing(lua_State* L, const char* type, const sol::object& key) {
    std::string member = type;
    if (key.get_type() == sol::type::string) {
        member += "." + key.as<std::string>();
    } else {
        sol::state_view lua(L);
        member += "[" + lua["tostring"](key).get<std::string>() + "]";
    }

    const Location where = callerLocation(L);
    Context& ctx = context(L);
    if (ctx.census.record(member, where.source, where.line)) {
        ctx.console.push(Level::System, where.source, member + " is not implemented (line " + std::to_string(where.line) + ")");
    }
}

} // namespace detail

namespace {

const char* typeName(lua_State* L, const sol::object& value) {
    return lua_typename(L, static_cast<int>(value.get_type()));
}

[[noreturn]] void badArgument(lua_State* L, size_t index, const char* what, const char* expected, const sol::object& got) {
    raise(L, "bad argument #" + std::to_string(index + 1) + " to '" + what + "' (" + expected + " expected, got " + typeName(L, got) + ")");
}

// A number, or a string that reads as one — the conversion NeoLua makes for a numeric
// parameter — and 0 for nil. Nothing otherwise.
std::optional<double> asNumber(const sol::object& value) {
    if (value.get_type() == sol::type::number) return value.as<double>();
    if (value.get_type() == sol::type::lua_nil || value.get_type() == sol::type::none) return 0.0;
    if (value.get_type() == sol::type::string) {
        const std::string text = value.as<std::string>();
        char* end = nullptr;
        const double parsed = std::strtod(text.c_str(), &end);
        if (end != text.c_str() && *end == '\0') return parsed;
    }
    return std::nullopt;
}

} // namespace

sol::object argument(const sol::variadic_args& args, size_t index) {
    return index < args.size() ? sol::object(args[index]) : sol::make_object(args.lua_state(), sol::lua_nil);
}

float argFloat(lua_State* L, const sol::variadic_args& args, size_t index, const char* what) {
    const sol::object value = argument(args, index);
    const std::optional<double> number = asNumber(value);
    if (!number) badArgument(L, index, what, "number", value);
    return static_cast<float>(*number);
}

int argInt(lua_State* L, const sol::variadic_args& args, size_t index, const char* what) {
    const sol::object value = argument(args, index);
    const std::optional<double> number = asNumber(value);
    if (!number) badArgument(L, index, what, "number", value);
    return static_cast<int>(std::lround(*number));
}

bool argBool(const sol::variadic_args& args, size_t index) {
    const sol::object value = argument(args, index);
    return value.valid() && value.get_type() != sol::type::lua_nil && !(value.get_type() == sol::type::boolean && !value.as<bool>());
}

std::string argString(lua_State* L, const sol::variadic_args& args, size_t index, const char* what) {
    const sol::object value = argument(args, index);
    if (value.get_type() == sol::type::string || value.get_type() == sol::type::number) return valueString(L, value);
    badArgument(L, index, what, "string", value);
}

bool argIsNil(const sol::variadic_args& args, size_t index) {
    const sol::object value = argument(args, index);
    return !value.valid() || value.get_type() == sol::type::lua_nil || value.get_type() == sol::type::none;
}

bool isInteger(lua_State* L, const sol::object& value) {
    value.push(L);
    const bool integer = lua_isinteger(L, -1) != 0;
    lua_pop(L, 1);
    return integer;
}

float valueFloat(lua_State* L, const sol::object& value, const char* what) {
    const std::optional<double> number = asNumber(value);
    if (!number) raise(L, std::string("cannot set ") + what + " to a " + typeName(L, value));
    return static_cast<float>(*number);
}

std::string valueString(lua_State* L, const sol::object& value) {
    if (value.get_type() == sol::type::string) return value.as<std::string>();
    sol::state_view lua(L);
    return lua["tostring"](value).get<std::string>();
}

} // namespace viewer::script
