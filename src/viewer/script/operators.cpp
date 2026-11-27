#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

#include "viewer/script/api.h"

// NeoLua's operators on nil and the booleans, as metatables on those two types, which Lua lets
// any type have through its C API. What each does was measured with scripts/neolua_oracle, and
// the "op ..." cases of test/viewer/lua_parity/cases.lua are the table this follows.
//
// NeoLua converts an operand to the other's type where .NET can: nil and false to 0, true to 1.
// An arithmetic or bitwise operator converts only its left operand, so nil + 1 is 1 and
// 1 + nil an error; ^, a comparison and .. convert either. A counter a script never set,
// `hits = hits + 1`, counts from 0 under Pathing, where 5.4 raises, and a tick handler doing it
// is dropped. Where NeoLua raises, these raise Lua's own message, without the variable's name
// Lua would add.
namespace viewer::script {

namespace {

enum class Arithmetic { Add, Subtract, Multiply, Divide, FloorDivide, Modulo, And, Or, Xor, ShiftLeft, ShiftRight };

bool isBitwise(Arithmetic op) {
    return op >= Arithmetic::And;
}

bool isNil(const sol::object& value) {
    return value.get_type() == sol::type::lua_nil || value.get_type() == sol::type::none;
}

bool isNilOrBoolean(const sol::object& value) {
    return isNil(value) || value.get_type() == sol::type::boolean;
}

// What NeoLua converts nil and a boolean to: 0 for nil and false, 1 for true.
lua_Integer integerOf(const sol::object& value) {
    return value.get_type() == sol::type::boolean && value.as<bool>() ? 1 : 0;
}

const char* typeName(lua_State* L, const sol::object& value) {
    return lua_typename(L, isNil(value) ? LUA_TNIL : static_cast<int>(value.get_type()));
}

// Lua's shift, which is no Int32 shift: past 63 places everything is shifted out.
lua_Integer shiftLeft(lua_Integer value, lua_Integer places) {
    const auto bits = static_cast<lua_Unsigned>(value);
    if (places <= -64 || places >= 64) return 0;
    return static_cast<lua_Integer>(places >= 0 ? bits << places : bits >> -places);
}

// nil or a boolean on the left, a number on the right; anything else raises.
sol::object arithmetic(sol::this_state state, Arithmetic op, const sol::object& a, const sol::object& b) {
    lua_State* L = state;
    if (!isNilOrBoolean(a) || b.get_type() != sol::type::number) {
        const sol::object& culprit = isNilOrBoolean(b) ? b : a;
        raise(
            L,
            std::string("attempt to perform ") + (isBitwise(op) ? "bitwise operation" : "arithmetic") + " on a " + typeName(L, culprit) +
                " value");
    }
    const lua_Integer left = integerOf(a);
    const bool integral = isInteger(L, b);
    const double right_float = b.as<double>();

    // A bitwise operator takes a float cut toward zero, as the binder hands one to an int.
    if (isBitwise(op)) {
        const double cut = std::trunc(right_float);
        if (!integral && !(cut >= -0x1p63 && cut < 0x1p63)) raise(L, "number has no integer representation");
        const lua_Integer right = integral ? b.as<lua_Integer>() : static_cast<lua_Integer>(cut);
        switch (op) {
        case Arithmetic::And: return sol::make_object(L, left & right);
        case Arithmetic::Or: return sol::make_object(L, left | right);
        case Arithmetic::Xor: return sol::make_object(L, left ^ right);
        case Arithmetic::ShiftLeft: return sol::make_object(L, shiftLeft(left, right));
        default: return sol::make_object(L, shiftLeft(left, right == LUA_MININTEGER ? 64 : -right));
        }
    }

    // NeoLua's `//` gives an integer, cut toward zero, and its `%` is .NET's remainder, with the
    // dividend's sign. Between numbers those stay Lua's (lua_parity_test.cpp lists them).
    const bool by_zero = integral && b.as<lua_Integer>() == 0;
    if (by_zero && op == Arithmetic::FloorDivide) raise(L, "attempt to perform 'n//0'");
    if (by_zero && op == Arithmetic::Modulo) raise(L, "attempt to perform 'n%0'");
    const auto unsigned_left = static_cast<lua_Unsigned>(left);
    switch (op) {
    case Arithmetic::Add:
        if (integral) return sol::make_object(L, static_cast<lua_Integer>(unsigned_left + static_cast<lua_Unsigned>(b.as<lua_Integer>())));
        return sol::make_object(L, static_cast<double>(left) + right_float);
    case Arithmetic::Subtract:
        if (integral) return sol::make_object(L, static_cast<lua_Integer>(unsigned_left - static_cast<lua_Unsigned>(b.as<lua_Integer>())));
        return sol::make_object(L, static_cast<double>(left) - right_float);
    case Arithmetic::Multiply:
        if (integral) return sol::make_object(L, left == 0 ? lua_Integer{0} : b.as<lua_Integer>());
        return sol::make_object(L, static_cast<double>(left) * right_float);
    case Arithmetic::Divide: return sol::make_object(L, static_cast<double>(left) / right_float);
    case Arithmetic::FloorDivide: {
        if (integral) return sol::make_object(L, left / b.as<lua_Integer>());
        const double quotient = std::trunc(static_cast<double>(left) / right_float);
        if (!(quotient >= -0x1p63 && quotient < 0x1p63)) raise(L, "number has no integer representation");
        return sol::make_object(L, static_cast<lua_Integer>(quotient));
    }
    default:
        if (integral) return sol::make_object(L, left % b.as<lua_Integer>());
        return sol::make_object(L, std::fmod(static_cast<double>(left), right_float));
    }
}

// ^ converts both operands to doubles, a string that reads as a number included.
double powerOperand(lua_State* L, const sol::object& value) {
    if (isNilOrBoolean(value)) return static_cast<double>(integerOf(value));
    if (value.get_type() == sol::type::number) return value.as<double>();
    if (value.get_type() == sol::type::string) {
        const std::string text = value.as<std::string>();
        if (lua_stringtonumber(L, text.c_str()) != 0) {
            const double number = lua_tonumber(L, -1);
            lua_pop(L, 1);
            return number;
        }
    }
    raise(L, std::string("attempt to perform arithmetic on a ") + typeName(L, value) + " value");
}

// A comparison converts either operand. nil orders before any string, as String.Compare orders
// null; NeoLua raises for nil < "a" but not for "a" > nil, which Lua compiles to the same test.
bool compare(lua_State* L, const sol::object& a, const sol::object& b, bool or_equal) {
    const auto comparable = [](const sol::object& value) -> std::optional<double> {
        if (isNilOrBoolean(value)) return static_cast<double>(integerOf(value));
        if (value.get_type() == sol::type::number) return value.as<double>();
        return std::nullopt;
    };
    if (isNil(a) && isNil(b)) raise(L, "attempt to compare two nil values");
    const std::optional<double> x = comparable(a);
    const std::optional<double> y = comparable(b);
    if (x && y) return or_equal ? *x <= *y : *x < *y;
    if (isNil(a) && b.get_type() == sol::type::string) return true;
    if (a.get_type() == sol::type::string && isNil(b)) return false;
    raise(L, std::string("attempt to compare ") + typeName(L, a) + " with " + typeName(L, b));
}

// .. writes nil as nothing and a boolean as tostring does, on either side, but hands itself to
// a table's or userdata's own __concat first: nil .. t is t's.
sol::object concat(sol::this_state state, const sol::object& a, const sol::object& b) {
    lua_State* L = state;
    for (const sol::object* operand : {&a, &b}) {
        if (operand->get_type() != sol::type::table && operand->get_type() != sol::type::userdata) continue;
        operand->push(L);
        if (luaL_getmetafield(L, -1, "__concat") == LUA_TNIL) {
            lua_pop(L, 1);
            continue;
        }
        const sol::protected_function method(L, -1);
        lua_pop(L, 2);
        sol::protected_function_result result = method(a, b);
        // Already placed by the error itself.
        if (!result.valid()) {
            const sol::error error = result;
            throw std::runtime_error(error.what());
        }
        return result.get<sol::object>();
    }
    const auto text = [L](const sol::object& value) -> std::string {
        if (isNil(value)) return "";
        if (value.get_type() == sol::type::boolean) return value.as<bool>() ? "true" : "false";
        return tostringText(L, value);
    };
    return sol::make_object(L, text(a) + text(b));
}

// The operators nil and the booleans share.
void setBinaryOperators(sol::table meta) {
    const auto arithmetic_of = [&meta](sol::meta_function name, Arithmetic op) {
        meta[name] = [op](sol::this_state state, const sol::object& a, const sol::object& b) { return arithmetic(state, op, a, b); };
    };
    arithmetic_of(sol::meta_function::addition, Arithmetic::Add);
    arithmetic_of(sol::meta_function::subtraction, Arithmetic::Subtract);
    arithmetic_of(sol::meta_function::multiplication, Arithmetic::Multiply);
    arithmetic_of(sol::meta_function::division, Arithmetic::Divide);
    arithmetic_of(sol::meta_function::floor_division, Arithmetic::FloorDivide);
    arithmetic_of(sol::meta_function::modulus, Arithmetic::Modulo);
    arithmetic_of(sol::meta_function::bitwise_and, Arithmetic::And);
    arithmetic_of(sol::meta_function::bitwise_or, Arithmetic::Or);
    arithmetic_of(sol::meta_function::bitwise_xor, Arithmetic::Xor);
    arithmetic_of(sol::meta_function::bitwise_left_shift, Arithmetic::ShiftLeft);
    arithmetic_of(sol::meta_function::bitwise_right_shift, Arithmetic::ShiftRight);
    meta[sol::meta_function::power_of] = [](sol::this_state state, const sol::object& a, const sol::object& b) {
        return std::pow(powerOperand(state, a), powerOperand(state, b));
    };
    meta[sol::meta_function::less_than] = [](sol::this_state state, const sol::object& a, const sol::object& b) {
        return compare(state, a, b, false);
    };
    meta[sol::meta_function::less_than_or_equal_to] = [](sol::this_state state, const sol::object& a, const sol::object& b) {
        return compare(state, a, b, true);
    };
    meta[sol::meta_function::concatenation] = concat;
}

// Sets `meta` as the metatable every value of the type of `push`'s value shares.
template <class Push>
void setTypeMetatable(lua_State* L, Push push, const sol::table& meta) {
    push(L);
    meta.push(L);
    lua_setmetatable(L, -2);
    lua_pop(L, 1);
}

} // namespace

void setNeoLuaOperators(sol::state_view lua) {
    lua_State* L = lua.lua_state();

    // nil has a length, 0, but no negation: -nil and ~nil raise in NeoLua too.
    sol::table nil_meta = lua.create_table();
    setBinaryOperators(nil_meta);
    nil_meta[sol::meta_function::length] = [] { return lua_Integer{0}; };
    setTypeMetatable(L, [](lua_State* state) { lua_pushnil(state); }, nil_meta);

    // A boolean negates as a float, -true is -1.0, and complements as an integer; it has no length.
    sol::table boolean_meta = lua.create_table();
    setBinaryOperators(boolean_meta);
    boolean_meta[sol::meta_function::unary_minus] = [](const sol::object& value) { return -static_cast<double>(integerOf(value)); };
    boolean_meta[sol::meta_function::bitwise_not] = [](const sol::object& value) { return ~integerOf(value); };
    setTypeMetatable(L, [](lua_State* state) { lua_pushboolean(state, 0); }, boolean_meta);
}

} // namespace viewer::script
