#include <imgui.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

#include "viewer/script/api.h"
#include "viewer/script/context.h"

namespace viewer::script {

namespace {

// .NET's float.ToString(): the shortest text that reads back as the same float.
std::string formatFloat(float value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

uint8_t toByte(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

// MonoGame's static Vector3.Dot(a, b) and friends are reached through an instance in
// NeoLua: `v:Dot(w)` arrives here as (w) once the shim has dropped `v`, and `v.Dot(a, b)` as
// (a, b). Both are the static call with the two vectors it was given.
std::pair<glm::vec3, glm::vec3> operands(lua_State* L, const Vector3& self, const sol::variadic_args& args, const char* what) {
    if (args.size() >= 2) return {toVector(L, args[0], what), toVector(L, args[1], what)};
    return {self.v, toVector(L, argument(args, 0), what)};
}

// Either side of an operator may be the number, as in `v * 2` and `2 * v`.
glm::vec3 vectorOrScalar(lua_State* L, const sol::object& value, const char* what) {
    if (value.get_type() == sol::type::number) return glm::vec3(value.as<float>());
    return toVector(L, value, what);
}

const Member<Vector3> kVector3Members[] = {
    {.name = "X",
     .get = [](sol::state_view lua, Vector3& self) { return sol::make_object(lua, self.v.x); },
     .set = [](sol::state_view lua, Vector3& self, const sol::object& value) { self.v.x = valueFloat(lua, value, "X"); }},
    {.name = "Y",
     .get = [](sol::state_view lua, Vector3& self) { return sol::make_object(lua, self.v.y); },
     .set = [](sol::state_view lua, Vector3& self, const sol::object& value) { self.v.y = valueFloat(lua, value, "Y"); }},
    {.name = "Z",
     .get = [](sol::state_view lua, Vector3& self) { return sol::make_object(lua, self.v.z); },
     .set = [](sol::state_view lua, Vector3& self, const sol::object& value) { self.v.z = valueFloat(lua, value, "Z"); }},
    {.name = "Length",
     .call = [](sol::state_view lua, Vector3& self, sol::variadic_args) { return sol::make_object(lua, glm::length(self.v)); }},
    {.name = "LengthSquared",
     .call = [](sol::state_view lua, Vector3& self, sol::variadic_args) { return sol::make_object(lua, glm::dot(self.v, self.v)); }},
    // The instance method normalizes in place and returns nothing, as MonoGame's does.
    {.name = "Normalize",
     .call =
         [](sol::state_view, Vector3& self, sol::variadic_args) {
             const float length = glm::length(self.v);
             if (length > 0.0f) self.v /= length;
             return sol::object(sol::lua_nil);
         }},
    {.name = "Dot",
     .call =
         [](sol::state_view lua, Vector3& self, sol::variadic_args args) {
             const auto [a, b] = operands(lua, self, args, "Dot");
             return sol::make_object(lua, glm::dot(a, b));
         }},
    {.name = "Cross",
     .call =
         [](sol::state_view lua, Vector3& self, sol::variadic_args args) {
             const auto [a, b] = operands(lua, self, args, "Cross");
             return pushVector(lua, glm::cross(a, b));
         }},
    {.name = "Distance",
     .call =
         [](sol::state_view lua, Vector3& self, sol::variadic_args args) {
             const auto [a, b] = operands(lua, self, args, "Distance");
             return sol::make_object(lua, glm::distance(a, b));
         }},
    {.name = "DistanceSquared",
     .call =
         [](sol::state_view lua, Vector3& self, sol::variadic_args args) {
             const auto [a, b] = operands(lua, self, args, "DistanceSquared");
             const glm::vec3 d = a - b;
             return sol::make_object(lua, glm::dot(d, d));
         }},
    {.name = "Equals",
     .call =
         [](sol::state_view lua, Vector3& self, sol::variadic_args args) {
             const sol::object other = argument(args, 0);
             return sol::make_object(lua, other.is<Vector3>() && other.as<Vector3>().v == self.v);
         }},
    {.name = "ToString",
     .call = [](sol::state_view lua, Vector3& self, sol::variadic_args) { return sol::make_object(lua, Vector3::toString(self)); }},
};

const Member<Color> kColorMembers[] = {
    {.name = "R",
     .get = [](sol::state_view lua, Color& self) { return sol::make_object(lua, static_cast<int>(self.r)); },
     .set = [](sol::state_view lua,
               Color& self,
               const sol::object& value) { self.r = toByte(static_cast<int>(valueFloat(lua, value, "R"))); }},
    {.name = "G",
     .get = [](sol::state_view lua, Color& self) { return sol::make_object(lua, static_cast<int>(self.g)); },
     .set = [](sol::state_view lua,
               Color& self,
               const sol::object& value) { self.g = toByte(static_cast<int>(valueFloat(lua, value, "G"))); }},
    {.name = "B",
     .get = [](sol::state_view lua, Color& self) { return sol::make_object(lua, static_cast<int>(self.b)); },
     .set = [](sol::state_view lua,
               Color& self,
               const sol::object& value) { self.b = toByte(static_cast<int>(valueFloat(lua, value, "B"))); }},
    {.name = "A",
     .get = [](sol::state_view lua, Color& self) { return sol::make_object(lua, static_cast<int>(self.a)); },
     .set = [](sol::state_view lua,
               Color& self,
               const sol::object& value) { self.a = toByte(static_cast<int>(valueFloat(lua, value, "A"))); }},
    {.name = "ToString",
     .call = [](sol::state_view lua, Color& self, sol::variadic_args) { return sol::make_object(lua, Color::toString(self)); }},
};

const Member<GuidValue> kGuidMembers[] = {
    // Scripting/Extensions/GuidExtensions.cs, the one member Pathing adds.
    {.name = "ToBase64",
     .call = [](sol::state_view lua, GuidValue& self, sol::variadic_args) { return sol::make_object(lua, toBase64(self.guid)); }},
    {.name = "ToString",
     .call = [](sol::state_view lua, GuidValue& self, sol::variadic_args) { return sol::make_object(lua, toString(self.guid)); }},
};

const Member<TimeSpan> kTimeSpanMembers[] = {
    {.name = "TotalSeconds", .get = [](sol::state_view lua, TimeSpan& self) { return sol::make_object(lua, self.seconds); }},
    {.name = "TotalMilliseconds", .get = [](sol::state_view lua, TimeSpan& self) { return sol::make_object(lua, self.seconds * 1000.0); }},
    {.name = "TotalMinutes", .get = [](sol::state_view lua, TimeSpan& self) { return sol::make_object(lua, self.seconds / 60.0); }},
    {.name = "TotalHours", .get = [](sol::state_view lua, TimeSpan& self) { return sol::make_object(lua, self.seconds / 3600.0); }},
    {.name = "TotalDays", .get = [](sol::state_view lua, TimeSpan& self) { return sol::make_object(lua, self.seconds / 86400.0); }},
    {.name = "Ticks",
     .get = [](sol::state_view lua, TimeSpan& self) { return sol::make_object(lua, static_cast<int64_t>(self.seconds * 1e7)); }},
    {.name = "Days",
     .get = [](sol::state_view lua, TimeSpan& self) { return sol::make_object(lua, static_cast<int>(self.seconds / 86400.0)); }},
    {.name = "Hours",
     .get = [](sol::state_view lua,
               TimeSpan& self) { return sol::make_object(lua, static_cast<int>(std::fmod(self.seconds / 3600.0, 24.0))); }},
    {.name = "Minutes",
     .get = [](sol::state_view lua,
               TimeSpan& self) { return sol::make_object(lua, static_cast<int>(std::fmod(self.seconds / 60.0, 60.0))); }},
    {.name = "Seconds",
     .get = [](sol::state_view lua, TimeSpan& self) { return sol::make_object(lua, static_cast<int>(std::fmod(self.seconds, 60.0))); }},
    {.name = "Milliseconds",
     .get = [](sol::state_view lua,
               TimeSpan& self) { return sol::make_object(lua, static_cast<int>(std::fmod(self.seconds * 1000.0, 1000.0))); }},
};

const Member<GameTime> kGameTimeMembers[] = {
    {.name = "TotalGameTime", .get = [](sol::state_view lua, GameTime& self) { return sol::make_object(lua, TimeSpan{self.total}); }},
    {.name = "ElapsedGameTime", .get = [](sol::state_view lua, GameTime& self) { return sol::make_object(lua, TimeSpan{self.elapsed}); }},
    {.name = "IsRunningSlowly", .get = [](sol::state_view lua, GameTime&) { return sol::make_object(lua, false); }},
};

const Member<Texture> kTextureMembers[] = {
    {.name = "HasTexture",
     .get = [](sol::state_view lua, Texture& self) { return sol::make_object(lua, self.asset_id < 0 && !self.path.empty()); }},
};

} // namespace

std::span<const Member<Vector3>> Vector3::members() {
    return kVector3Members;
}

std::string Vector3::toString(const Vector3& value) {
    return "{X:" + formatFloat(value.v.x) + " Y:" + formatFloat(value.v.y) + " Z:" + formatFloat(value.v.z) + "}";
}

void Vector3::extend(sol::usertype<Vector3>& type) {
    type[sol::meta_function::addition] = [](sol::this_state L, const sol::object& a, const sol::object& b) {
        return Vector3{vectorOrScalar(L, a, "+") + vectorOrScalar(L, b, "+")};
    };
    type[sol::meta_function::subtraction] = [](sol::this_state L, const sol::object& a, const sol::object& b) {
        return Vector3{vectorOrScalar(L, a, "-") - vectorOrScalar(L, b, "-")};
    };
    type[sol::meta_function::multiplication] = [](sol::this_state L, const sol::object& a, const sol::object& b) {
        return Vector3{vectorOrScalar(L, a, "*") * vectorOrScalar(L, b, "*")};
    };
    type[sol::meta_function::division] = [](sol::this_state L, const sol::object& a, const sol::object& b) {
        return Vector3{vectorOrScalar(L, a, "/") / vectorOrScalar(L, b, "/")};
    };
    type[sol::meta_function::unary_minus] = [](const Vector3& a) { return Vector3{-a.v}; };
    type[sol::meta_function::equal_to] = [](const Vector3& a, const Vector3& b) { return a.v == b.v; };
}

std::span<const Member<Color>> Color::members() {
    return kColorMembers;
}

std::string Color::toString(const Color& value) {
    return "{R:" + std::to_string(value.r) + " G:" + std::to_string(value.g) + " B:" + std::to_string(value.b) +
           " A:" + std::to_string(value.a) + "}";
}

void Color::extend(sol::usertype<Color>& type) {
    type[sol::meta_function::equal_to] = [](const Color& a, const Color& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
}

std::span<const Member<GuidValue>> GuidValue::members() {
    return kGuidMembers;
}

std::string GuidValue::toString(const GuidValue& value) {
    return viewer::toString(value.guid);
}

void GuidValue::extend(sol::usertype<GuidValue>& type) {
    type[sol::meta_function::equal_to] = [](const GuidValue& a, const GuidValue& b) { return a.guid == b.guid; };
    // "x" .. marker.Guid, which .NET's string concatenation makes the Guid's ToString.
    type[sol::meta_function::concatenation] = [](sol::this_state L, const sol::object& a, const sol::object& b) {
        return valueString(L, a) + valueString(L, b);
    };
}

std::span<const Member<TimeSpan>> TimeSpan::members() {
    return kTimeSpanMembers;
}

std::string TimeSpan::toString(const TimeSpan& value) {
    // TimeSpan's constant format: [-][d.]hh:mm:ss[.fffffff]
    const bool negative = value.seconds < 0.0;
    int64_t ticks = static_cast<int64_t>(std::llround(std::abs(value.seconds) * 1e7));
    const int64_t days = ticks / 864000000000LL;
    ticks %= 864000000000LL;
    const int hours = static_cast<int>(ticks / 36000000000LL);
    ticks %= 36000000000LL;
    const int minutes = static_cast<int>(ticks / 600000000LL);
    ticks %= 600000000LL;
    const int seconds = static_cast<int>(ticks / 10000000LL);
    const int fraction = static_cast<int>(ticks % 10000000LL);

    std::string text = negative ? "-" : "";
    if (days > 0) text += std::format("{}.", days);
    text += std::format("{:02}:{:02}:{:02}", hours, minutes, seconds);
    if (fraction > 0) text += std::format(".{:07}", fraction);
    return text;
}

std::span<const Member<GameTime>> GameTime::members() {
    return kGameTimeMembers;
}

std::span<const Member<Texture>> Texture::members() {
    return kTextureMembers;
}

sol::object pushVector(sol::state_view lua, const glm::vec3& blish) {
    return sol::make_object(lua, Vector3{blish});
}

sol::object pushColor(sol::state_view lua, uint32_t colour) {
    Color color;
    color.r = static_cast<uint8_t>(colour >> IM_COL32_R_SHIFT);
    color.g = static_cast<uint8_t>(colour >> IM_COL32_G_SHIFT);
    color.b = static_cast<uint8_t>(colour >> IM_COL32_B_SHIFT);
    color.a = static_cast<uint8_t>(colour >> IM_COL32_A_SHIFT);
    return sol::make_object(lua, color);
}

sol::object pushGuid(sol::state_view lua, const Guid& guid) {
    Context& ctx = context(lua.lua_state());
    const std::string key(guid.bytes.begin(), guid.bytes.end());
    auto [it, inserted] = ctx.guid_objects.try_emplace(key);
    if (inserted) it->second = sol::make_object(lua, GuidValue{guid});
    return it->second;
}

glm::vec3 toVector(lua_State* L, const sol::object& value, const char* what) {
    if (value.is<Vector3>()) return value.as<Vector3>().v;
    if (value.get_type() == sol::type::lua_nil) return glm::vec3(0.0f);
    raise(
        L,
        std::string("bad argument to '") + what + "' (Vector3 expected, got " + lua_typename(L, static_cast<int>(value.get_type())) + ")");
}

uint32_t toColor(lua_State* L, const sol::object& value, const char* what) {
    if (value.is<Color>()) {
        const Color& color = value.as<Color>();
        return IM_COL32(color.r, color.g, color.b, color.a);
    }
    if (value.get_type() == sol::type::lua_nil) return IM_COL32(0, 0, 0, 0);
    raise(
        L,
        std::string("bad argument to '") + what + "' (Color expected, got " + lua_typename(L, static_cast<int>(value.get_type())) + ")");
}

Guid toGuid(lua_State* L, const sol::object& value, const char* what) {
    if (value.is<GuidValue>()) return value.as<GuidValue>().guid;
    if (value.get_type() == sol::type::lua_nil) return Guid{};
    // Anything else as .NET hands it to a string parameter, which is what World's lookups
    // take: its ToString(). Hero's LAA_Fade passes markers to MarkerByGuid this way, and finds
    // nothing, as it does in Pathing.
    (void)what;
    return guidFromText(valueString(L, value));
}

glm::vec3 toBlish(const glm::vec3& file) {
    return glm::vec3(file.x, file.z, file.y);
}

glm::vec3 fromBlish(const glm::vec3& blish) {
    return glm::vec3(blish.x, blish.z, blish.y);
}

void bindValues(sol::state_view lua) {
    bindType<Vector3>(lua);
    bindType<Color>(lua);
    bindType<GuidValue>(lua);
    bindType<TimeSpan>(lua);
    bindType<GameTime>(lua);
    bindType<Texture>(lua);
}

} // namespace viewer::script
