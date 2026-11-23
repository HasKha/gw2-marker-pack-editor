#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <string>

#include "viewer/guid.h"
#include "viewer/script/object.h"

// The Pathing API as Lua sees it: one struct per Lua-visible type, each with its member table
// (see object.h). Values are copied in and out as the .NET structs they stand for are; handles
// are indices into the live map, the category tree or the engine's menus.
namespace viewer::script {

// ── Values ────────────────────────────────────────────────────────────────────

// Microsoft.Xna.Framework.Vector3, in Blish HUD's space: metres, Z up.
struct Vector3 {
    glm::vec3 v{0.0f};
    static constexpr const char* kName = "Vector3";
    static std::span<const Member<Vector3>> members();
    static std::string toString(const Vector3& value);
    static void extend(sol::usertype<Vector3>& type);
};

// Microsoft.Xna.Framework.Color: byte channels.
struct Color {
    uint8_t r = 255, g = 255, b = 255, a = 255;
    static constexpr const char* kName = "Color";
    static std::span<const Member<Color>> members();
    static std::string toString(const Color& value);
    static void extend(sol::usertype<Color>& type);
};

struct GuidValue {
    Guid guid;
    static constexpr const char* kName = "Guid";
    static std::span<const Member<GuidValue>> members();
    static std::string toString(const GuidValue& value);
    static void extend(sol::usertype<GuidValue>& type);
};

// System.TimeSpan, as seconds.
struct TimeSpan {
    double seconds = 0.0;
    static constexpr const char* kName = "TimeSpan";
    static std::span<const Member<TimeSpan>> members();
    static std::string toString(const TimeSpan& value);
};

// Microsoft.Xna.Framework.GameTime, as Event:OnTick hands it.
struct GameTime {
    double total = 0.0;
    double elapsed = 0.0;
    static constexpr const char* kName = "GameTime";
    static std::span<const Member<GameTime>> members();
};

// Blish HUD's AsyncTexture2D: an image of one pack, by path. An asset id stands for a texture
// from the game's render service, which is not supported; it draws nothing.
struct Texture {
    size_t pack = 0;
    std::string path;
    int asset_id = -1;
    static constexpr const char* kName = "AsyncTexture2D";
    static std::span<const Member<Texture>> members();
};

// ── Handles ───────────────────────────────────────────────────────────────────

struct Marker {
    size_t index = 0; // into LiveMap::markers
    static constexpr const char* kName = "StandardMarker";
    static std::span<const Member<Marker>> members();
};

struct Trail {
    size_t index = 0; // into LiveMap::trails
    static constexpr const char* kName = "StandardTrail";
    static std::span<const Member<Trail>> members();
};

// One of a marker's or trail's behaviors, as GetBehavior hands it over by its class name in
// Pathing: what the script reads and sets on it reaches the live pathable's own behaviors.
struct Behavior {
    enum class Kind : uint8_t {
        FestivalFilter,
        MountFilter,
        ProfessionFilter,
        RaceFilter,
        SpecializationFilter,
        MapTypeFilter,
        ScheduleFilter,
        StandardBehaviorFilter,
        InfoModifier,
        BounceModifier,
        CopyModifier,
        ToggleModifier,
        ResetGuidModifier,
        ShowHideModifier,
        Script,
    };
    bool trail = false;
    size_t index = 0; // into LiveMap::markers or ::trails
    Kind kind = Kind::Script;
    static constexpr const char* kName = "Behavior";
    static std::span<const Member<Behavior>> members();
};

// A node of the shared category tree, or the root above every pack's top level, which
// Pathing has as a category of its own (World.RootCategory).
struct Category {
    static constexpr size_t kRoot = static_cast<size_t>(-2); // not kNoIndex, which is "none"
    size_t node = kRoot;
    static constexpr const char* kName = "PathingCategory";
    static std::span<const Member<Category>> members();
    static void extend(sol::usertype<Category>& type);
};

struct Menu {
    size_t index = 0; // into Context::menus
    static constexpr const char* kName = "Menu";
    static std::span<const Member<Menu>> members();
};

// The `Pack` a chunk is handed: Pathing's PackContext.
struct PackContext {
    size_t pack = 0;
    static constexpr const char* kName = "PackContext";
    static std::span<const Member<PackContext>> members();
};

// ── Pushing ───────────────────────────────────────────────────────────────────

sol::object pushVector(sol::state_view lua, const glm::vec3& blish);
sol::object pushColor(sol::state_view lua, uint32_t imgui_colour); // an ImU32
sol::object pushGuid(sol::state_view lua, const Guid& guid);
sol::object pushMarker(sol::state_view lua, size_t index);
sol::object pushTrail(sol::state_view lua, size_t index);
sol::object pushCategory(sol::state_view lua, size_t node); // nil for kNoIndex
sol::object pushMenu(sol::state_view lua, size_t index);

// A value a script passes where a .NET method wants one of these. Raises otherwise.
glm::vec3 toVector(lua_State* L, const sol::object& value, const char* what);
uint32_t toColor(lua_State* L, const sol::object& value, const char* what); // an ImU32
Guid toGuid(lua_State* L, const sol::object& value, const char* what); // a Guid, or any value as its text

// Between the pack files' space (Y up) and Blish's (Z up): the swap TmfLib makes on reading.
glm::vec3 toBlish(const glm::vec3& file);
glm::vec3 fromBlish(const glm::vec3& blish);

// ── Binding ───────────────────────────────────────────────────────────────────

void bindValues(sol::state_view lua);
void bindWorld(sol::state_view lua); // Marker, Trail, Category, World
// Everything PathingGlobal holds, into `env`: the API objects and the sandboxed base library.
void bindGlobals(sol::state_view lua, sol::table env);
// Pathing's own `os` (Scripting/Lib/Std/LuaLibraryOS.cs), which is not Lua's.
sol::table makeOsLibrary(sol::state_view lua);

} // namespace viewer::script
