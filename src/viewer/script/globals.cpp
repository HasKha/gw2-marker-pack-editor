#include <glm/geometric.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>

#include "pack/attributes.h"
#include "viewer/game/mumble.h"
#include "viewer/live/runtime.h"
#include "viewer/packs/registry.h"
#include "viewer/script/api.h"
#include "viewer/script/context.h"
#include "viewer/script/lson.h"
#include "viewer/store.h"

namespace viewer::script {

namespace {

const MumbleState& mumble(lua_State* L) {
    return *context(L).host.mumble;
}

// What a .NET string parameter makes of any value: its text, and nil for null.
std::string textOf(lua_State* L, const sol::object& value) {
    return value.get_type() == sol::type::lua_nil || value.get_type() == sol::type::none ? std::string() : valueString(L, value);
}

// A read-only property of the Mumble snapshot.
template <class Api, auto Field>
Member<Api> mumbleField(const char* name) {
    return {.name = name, .get = [](sol::state_view lua, Api&) { return sol::make_object(lua, mumble(lua).*Field); }};
}

// An enum property, which a script sees by its name.
template <class Api, auto Field, Gw2Enum Type>
Member<Api> mumbleEnum(const char* name) {
    return {.name = name, .get = [](sol::state_view lua, Api&) { return sol::make_object(lua, enumName(Type, mumble(lua).*Field)); }};
}

template <class Api, auto Field>
Member<Api> mumbleVector(const char* name) {
    return {.name = name, .get = [](sol::state_view lua, Api&) { return pushVector(lua, mumble(lua).*Field); }};
}

// ── Mumble ────────────────────────────────────────────────────────────────────

struct CurrentMapApi {
    static constexpr const char* kName = "CurrentMap";
    static std::span<const Member<CurrentMapApi>> members();
};
struct PlayerCharacterApi {
    static constexpr const char* kName = "PlayerCharacter";
    static std::span<const Member<PlayerCharacterApi>> members();
};
struct PlayerCameraApi {
    static constexpr const char* kName = "PlayerCamera";
    static std::span<const Member<PlayerCameraApi>> members();
};
struct UiApi {
    static constexpr const char* kName = "UI";
    static std::span<const Member<UiApi>> members();
};
struct InfoApi {
    static constexpr const char* kName = "Info";
    static std::span<const Member<InfoApi>> members();
};
struct MumbleApi {
    static constexpr const char* kName = "Gw2MumbleService";
    static std::span<const Member<MumbleApi>> members();
};

const Member<CurrentMapApi> kCurrentMapMembers[] = {
    mumbleField<CurrentMapApi, &MumbleState::map_id>("Id"),
    mumbleEnum<CurrentMapApi, &MumbleState::map_type, Gw2Enum::MapType>("Type"),
    mumbleField<CurrentMapApi, &MumbleState::is_competitive>("IsCompetitiveMode"),
};

const Member<PlayerCharacterApi> kPlayerCharacterMembers[] = {
    mumbleVector<PlayerCharacterApi, &MumbleState::player_position>("Position"),
    mumbleVector<PlayerCharacterApi, &MumbleState::player_forward>("Forward"),
    mumbleField<PlayerCharacterApi, &MumbleState::name>("Name"),
    mumbleEnum<PlayerCharacterApi, &MumbleState::profession, Gw2Enum::Profession>("Profession"),
    mumbleEnum<PlayerCharacterApi, &MumbleState::race, Gw2Enum::Race>("Race"),
    mumbleField<PlayerCharacterApi, &MumbleState::specialization>("Specialization"),
    mumbleField<PlayerCharacterApi, &MumbleState::team_color_id>("TeamColorId"),
    mumbleField<PlayerCharacterApi, &MumbleState::is_commander>("IsCommander"),
    mumbleField<PlayerCharacterApi, &MumbleState::is_in_combat>("IsInCombat"),
    mumbleEnum<PlayerCharacterApi, &MumbleState::mount, Gw2Enum::Mount>("CurrentMount"),
};

const Member<PlayerCameraApi> kPlayerCameraMembers[] = {
    mumbleVector<PlayerCameraApi, &MumbleState::camera_position>("Position"),
    mumbleVector<PlayerCameraApi, &MumbleState::camera_forward>("Forward"),
    mumbleField<PlayerCameraApi, &MumbleState::field_of_view>("FieldOfView"),
    // Blish HUD's own constants (PlayerCamera.cs), not the game's.
    {.name = "NearPlaneRenderDistance", .get = [](sol::state_view lua, PlayerCameraApi&) { return sol::make_object(lua, 0.01f); }},
    {.name = "FarPlaneRenderDistance", .get = [](sol::state_view lua, PlayerCameraApi&) { return sol::make_object(lua, 1000.0f); }},
};

const Member<UiApi> kUiMembers[] = {
    mumbleField<UiApi, &MumbleState::compass_rotation>("CompassRotation"),
    mumbleField<UiApi, &MumbleState::is_compass_rotation_enabled>("IsCompassRotationEnabled"),
    mumbleField<UiApi, &MumbleState::is_compass_top_right>("IsCompassTopRight"),
    mumbleField<UiApi, &MumbleState::is_map_open>("IsMapOpen"),
    mumbleField<UiApi, &MumbleState::map_scale>("MapScale"),
    mumbleField<UiApi, &MumbleState::is_text_input_focused>("IsTextInputFocused"),
    mumbleEnum<UiApi, &MumbleState::ui_size, Gw2Enum::UiSize>("UISize"),
};

const Member<InfoApi> kInfoMembers[] = {
    mumbleField<InfoApi, &MumbleState::build_id>("BuildId"),
    mumbleField<InfoApi, &MumbleState::is_game_focused>("IsGameFocused"),
    mumbleField<InfoApi, &MumbleState::shard_id>("ShardId"),
    mumbleField<InfoApi, &MumbleState::process_id>("ProcessId"),
};

const Member<MumbleApi> kMumbleMembers[] = {
    mumbleField<MumbleApi, &MumbleState::available>("IsAvailable"),
    mumbleField<MumbleApi, &MumbleState::tick>("Tick"),
    {.name = "CurrentMap", .get = [](sol::state_view lua, MumbleApi&) { return sol::make_object(lua, CurrentMapApi{}); }},
    {.name = "PlayerCharacter", .get = [](sol::state_view lua, MumbleApi&) { return sol::make_object(lua, PlayerCharacterApi{}); }},
    {.name = "PlayerCamera", .get = [](sol::state_view lua, MumbleApi&) { return sol::make_object(lua, PlayerCameraApi{}); }},
    {.name = "UI", .get = [](sol::state_view lua, MumbleApi&) { return sol::make_object(lua, UiApi{}); }},
    {.name = "Info", .get = [](sol::state_view lua, MumbleApi&) { return sol::make_object(lua, InfoApi{}); }},
};

std::span<const Member<CurrentMapApi>> CurrentMapApi::members() {
    return kCurrentMapMembers;
}
std::span<const Member<PlayerCharacterApi>> PlayerCharacterApi::members() {
    return kPlayerCharacterMembers;
}
std::span<const Member<PlayerCameraApi>> PlayerCameraApi::members() {
    return kPlayerCameraMembers;
}
std::span<const Member<UiApi>> UiApi::members() {
    return kUiMembers;
}
std::span<const Member<InfoApi>> InfoApi::members() {
    return kInfoMembers;
}
std::span<const Member<MumbleApi>> MumbleApi::members() {
    return kMumbleMembers;
}

// ── Debug ─────────────────────────────────────────────────────────────────────

struct DebugApi {
    static constexpr const char* kName = "Debug";
    static std::span<const Member<DebugApi>> members();
};

template <Level Of>
Member<DebugApi> debugMessage(const char* name) {
    return {.name = name, .call = [](sol::state_view lua, DebugApi&, sol::variadic_args args) {
                context(lua).console.push(Of, callerLocation(lua).source, textOf(lua, argument(args, 0)));
                return sol::object(sol::lua_nil);
            }};
}

const Member<DebugApi> kDebugMembers[] = {
    debugMessage<Level::Info>("Print"),
    debugMessage<Level::Info>("Info"),
    debugMessage<Level::Warn>("Warn"),
    debugMessage<Level::Error>("Error"),
    {.name = "ShowMessage",
     .call =
         [](sol::state_view lua, DebugApi&, sol::variadic_args args) {
             const std::string text = textOf(lua, argument(args, 0));
             const Context& ctx = context(lua);
             if (ctx.host.show_message) ctx.host.show_message(text);
             context(lua).console.push(Level::Info, callerLocation(lua).source, text);
             return sol::object(sol::lua_nil);
         }},
    {.name = "Watch",
     .call =
         [](sol::state_view lua, DebugApi&, sol::variadic_args args) {
             const sol::object value = argument(args, 1);
             context(lua).watches[argString(lua, args, 0, "Watch")] = valueString(lua, value);
             return sol::object(sol::lua_nil);
         }},
    {.name = "ClearWatch",
     .call =
         [](sol::state_view lua, DebugApi&, sol::variadic_args args) {
             context(lua).watches.erase(argString(lua, args, 0, "ClearWatch"));
             return sol::object(sol::lua_nil);
         }},
};

std::span<const Member<DebugApi>> DebugApi::members() {
    return kDebugMembers;
}

// ── Event ─────────────────────────────────────────────────────────────────────

struct EventApi {
    static constexpr const char* kName = "Event";
    static std::span<const Member<EventApi>> members();
};

// The pack a function was written in, from its chunk; a C function belongs to whoever called.
size_t packOfFunction(lua_State* L, const sol::object& function) {
    Context& ctx = context(L);
    function.push(L);
    lua_Debug info{};
    lua_getinfo(L, ">S", &info);
    std::string_view source = info.source ? info.source : "";
    if (source.starts_with('@')) source.remove_prefix(1);
    const size_t pack = ctx.packOf(std::string(source));
    return pack == Context::kNoPack ? ctx.packOf(callerLocation(L).source) : pack;
}

const Member<EventApi> kEventMembers[] = {
    {.name = "OnTick",
     .call =
         [](sol::state_view lua, EventApi&, sol::variadic_args args) {
             const sol::object callback = argument(args, 0);
             if (callback.get_type() == sol::type::function) {
                 context(lua).tick_listeners.push_back({sol::protected_function(callback), packOfFunction(lua, callback)});
             }
             return sol::object(sol::lua_nil);
         }},
};

std::span<const Member<EventApi>> EventApi::members() {
    return kEventMembers;
}

// ── Storage ───────────────────────────────────────────────────────────────────

struct StorageApi {
    static constexpr const char* kName = "Storage";
    static std::span<const Member<StorageApi>> members();
};

bool isBlank(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); });
}

// Lib/Storage.cs ValidateKey: "<namespace>\<name>", the namespace "000" when none is given.
// Every method takes (namespace, name, ...) or just (name, ...), told apart by how many
// arguments there are, as the .NET overloads are.
std::string storageKey(lua_State* L, const sol::variadic_args& args, size_t with_namespace, size_t* first_after) {
    const bool has_namespace = args.size() >= with_namespace;
    const std::string ns = has_namespace && !argIsNil(args, 0) ? valueString(L, args[0]) : "000";
    const size_t name_at = has_namespace ? 1 : 0;
    const std::string name = argString(L, args, name_at, "Storage");
    *first_after = name_at + 1;

    if (ns.size() > 64 || isBlank(ns)) raise(L, "Key namespace must be nil or between 1 and 64 characters");
    if (name.size() > 64 || isBlank(name)) raise(L, "Key name must be between 1 and 64 characters");
    return ns + "\\" + name;
}

const Member<StorageApi> kStorageMembers[] = {
    {.name = "UpsertValue",
     .call =
         [](sol::state_view lua, StorageApi&, sol::variadic_args args) {
             size_t value_at = 0;
             const std::string key = storageKey(lua, args, 3, &value_at);
             const sol::object value = argument(args, value_at);
             return sol::make_object(lua, context(lua).host.storage->upsert(key, textOf(lua, value)));
         }},
    {.name = "ReadValue",
     .call = [](sol::state_view lua, StorageApi&, sol::variadic_args args) -> sol::object {
         size_t unused = 0;
         const std::optional<std::string> value = context(lua).host.storage->read(storageKey(lua, args, 2, &unused));
         return value ? sol::make_object(lua, *value) : sol::object(sol::lua_nil);
     }},
    {.name = "DeleteValue",
     .call =
         [](sol::state_view lua, StorageApi&, sol::variadic_args args) {
             size_t unused = 0;
             context(lua).host.storage->erase(storageKey(lua, args, 2, &unused));
             return sol::object(sol::lua_nil);
         }},
};

std::span<const Member<StorageApi>> StorageApi::members() {
    return kStorageMembers;
}

// ── User ──────────────────────────────────────────────────────────────────────

struct UserApi {
    static constexpr const char* kName = "User";
    static std::span<const Member<UserApi>> members();
};

const Member<UserApi> kUserMembers[] = {
    {.name = "SetClipboard",
     .call =
         [](sol::state_view lua, UserApi&, sol::variadic_args args) {
             const std::string value = textOf(lua, argument(args, 0));
             // CopyModifier.DEFAULT_COPYMESSAGE
             const std::string message = args.size() > 1 ? textOf(lua, args[1]) : "'" + value + "' copied to clipboard.";
             const Host& host = context(lua).host;
             return sol::make_object(lua, host.set_clipboard && host.set_clipboard(value, message));
         }},
    {.name = "ShowInfo",
     .call =
         [](sol::state_view lua, UserApi&, sol::variadic_args args) {
             const std::string message = textOf(lua, argument(args, 0));
             context(lua).info.push_back(message);
             return sol::make_object(lua, message);
         }},
    {.name = "HideInfo",
     .call =
         [](sol::state_view lua, UserApi&, sol::variadic_args args) {
             std::vector<std::string>& info = context(lua).info;
             const auto it = std::find(info.begin(), info.end(), textOf(lua, argument(args, 0)));
             if (it != info.end()) info.erase(it);
             return sol::object(sol::lua_nil);
         }},
};

std::span<const Member<UserApi>> UserApi::members() {
    return kUserMembers;
}

// ── Pathing ───────────────────────────────────────────────────────────────────

struct PathingApi {
    static constexpr const char* kName = "Pathing";
    static std::span<const Member<PathingApi>> members();
};

// major.minor.patch, anything after a '-' or '+' ignored. Nothing when it does not parse,
// which SemVer.Version answers by throwing and Pathing by returning false.
std::optional<std::array<int, 3>> parseVersion(std::string_view text) {
    std::array<int, 3> parts{};
    for (size_t i = 0; i < 3; ++i) {
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parts[i]);
        if (error != std::errc()) return std::nullopt;
        text.remove_prefix(static_cast<size_t>(end - text.data()));
        if (i < 2) {
            if (!text.starts_with('.')) return std::nullopt;
            text.remove_prefix(1);
        }
    }
    if (!text.empty() && text.front() != '-' && text.front() != '+') return std::nullopt;
    return parts;
}

const Member<PathingApi> kPathingMembers[] = {
    {.name = "Version", .get = [](sol::state_view lua, PathingApi&) { return sol::make_object(lua, kPathingVersion); }},
    {.name = "IsVersionAtLeast",
     .call =
         [](sol::state_view lua, PathingApi&, sol::variadic_args args) {
             const std::optional<std::array<int, 3>> wanted = parseVersion(textOf(lua, argument(args, 0)));
             return sol::make_object(lua, wanted && *parseVersion(kPathingVersion) >= *wanted);
         }},
};

std::span<const Member<PathingApi>> PathingApi::members() {
    return kPathingMembers;
}

// ── Markers scripts create ────────────────────────────────────────────────────

// Lib/Instance.cs AttributeCollectionFromLuaTable: each named member, as
// string.Format(InvariantCulture, "{0}", value) writes it — so `true` is "True", which
// TmfLib's bool parsing reads as true all the same.
std::string attributeText(lua_State* L, const sol::object& value) {
    if (value.get_type() == sol::type::boolean) return value.as<bool>() ? "True" : "False";
    if (value.get_type() == sol::type::number && !isInteger(L, value)) return pack::formatFloat(value.as<float>());
    return valueString(L, value);
}

// I:Marker / Pack:CreateMarker: a marker on the live map of the calling pack, its attributes
// resolved against that pack's categories as a pack's own markers are. Pathing gives a
// missing coordinate 0, and an unknown map the current one.
sol::object createMarker(sol::state_view lua, size_t pack_index, const sol::object& attributes) {
    Context& ctx = context(lua);
    const runtime::Installed& installed = *ctx.host.installed;
    const pack::Pack& declared = installed.packs.at(pack_index)->loaded->declared;

    pack::Pathable pathable;
    pathable.type = pack::PathableType::Poi;
    if (attributes.get_type() == sol::type::table) {
        for (const auto& [key, value] : attributes.as<sol::table>()) {
            if (key.get_type() == sol::type::string) pathable.attributes.set(key.as<std::string>(), attributeText(lua, value));
        }
    }
    for (const char* axis : {"xpos", "ypos", "zpos"}) {
        if (!pathable.attributes.value(axis)) pathable.attributes.set(axis, "0");
    }
    if (const std::string* type = pathable.attributes.value("type")) {
        pathable.category_id = pack::attributeKey(*type);
        const pack::Category* category = declared.findCategory(pathable.category_id);
        pathable.category = category ? static_cast<size_t>(category - declared.categories.data()) : pack::kNoIndex;
    }

    runtime::LiveMarker marker;
    marker.pack = pack_index;
    marker.texture_pack = pack_index;
    marker.node = installed.tree.find(pathable.category_id);
    // Made even when it cannot be placed, from a default bake.
    marker.marker = bakeMarker(declared, pathable).value_or(BakedMarker{});
    if (!declared.effectiveValue(pathable, "mapid") || marker.marker.map_id < 0) marker.marker.map_id = mumble(lua).map_id;

    runtime::LiveMap& live = *ctx.host.live;
    live.markers.push_back(std::move(marker));
    return pushMarker(lua, live.markers.size() - 1);
}

size_t packArgument(lua_State* L, const sol::object& value, const char* what) {
    if (!value.is<PackContext>()) raise(L, std::string("bad argument #1 to '") + what + "' (PackContext expected)");
    return value.as<PackContext>().pack;
}

// ── I ─────────────────────────────────────────────────────────────────────────

struct InstanceApi {
    static constexpr const char* kName = "Instance";
    static std::span<const Member<InstanceApi>> members();
};

const Member<InstanceApi> kInstanceMembers[] = {
    {.name = "Vector3",
     .call =
         [](sol::state_view lua, InstanceApi&, sol::variadic_args args) {
             return pushVector(
                 lua,
                 {argFloat(lua, args, 0, "Vector3"), argFloat(lua, args, 1, "Vector3"), argFloat(lua, args, 2, "Vector3")});
         }},
    // new Color(int r, int g, int b, int alpha), which clamps each channel.
    {.name = "Color",
     .call =
         [](sol::state_view lua, InstanceApi&, sol::variadic_args args) {
             const auto channel = [&](size_t index) { return static_cast<uint8_t>(std::clamp(argInt(lua, args, index, "Color"), 0, 255)); };
             const uint8_t alpha = argIsNil(args, 3) ? 255 : channel(3);
             return sol::make_object(lua, Color{channel(0), channel(1), channel(2), alpha});
         }},
    {.name = "Guid",
     .call = [](sol::state_view lua,
                InstanceApi&,
                sol::variadic_args args) { return pushGuid(lua, guidFromText(argString(lua, args, 0, "Guid"))); }},
    {.name = "Texture",
     .call =
         [](sol::state_view lua, InstanceApi&, sol::variadic_args args) {
             if (args.size() == 1) return sol::make_object(lua, Texture{0, {}, argInt(lua, args, 0, "Texture")});
             return sol::make_object(lua, Texture{packArgument(lua, argument(args, 0), "Texture"), argString(lua, args, 1, "Texture")});
         }},
    {.name = "Marker",
     .call =
         [](sol::state_view lua, InstanceApi&, sol::variadic_args args) {
             const sol::object attributes = argument(args, 1);
             return createMarker(lua, packArgument(lua, argument(args, 0), "Marker"), attributes);
         }},
};

std::span<const Member<InstanceApi>> InstanceApi::members() {
    return kInstanceMembers;
}

} // namespace

// ── PackContext ───────────────────────────────────────────────────────────────

namespace {

const Member<PackContext> kPackMembers[] = {
    {.name = "Require",
     .call = [](sol::state_view lua,
                PackContext& self,
                sol::variadic_args args) { return requireScript(lua, self.pack, argString(lua, args, 0, "Require")); }},
    {.name = "CreateMarker",
     .call =
         [](sol::state_view lua, PackContext& self, sol::variadic_args args) { return createMarker(lua, self.pack, argument(args, 0)); }},
};

} // namespace

std::span<const Member<PackContext>> PackContext::members() {
    return kPackMembers;
}

// ── Menu ──────────────────────────────────────────────────────────────────────

namespace {

MenuNode& menuNode(lua_State* L, const Menu& menu) {
    return context(L).menus.at(menu.index);
}

template <auto Field>
Member<Menu> menuFlag(const char* name) {
    return {
        .name = name,
        .get = [](sol::state_view lua, Menu& self) { return sol::make_object(lua, menuNode(lua, self).*Field); },
        .set = [](sol::state_view lua, Menu& self, const sol::object& value) { menuNode(lua, self).*Field = value.as<bool>(); }};
}

bool sameName(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

const Member<Menu> kMenuMembers[] = {
    {.name = "Name", .get = [](sol::state_view lua, Menu& self) { return sol::make_object(lua, menuNode(lua, self).name); }},
    {.name = "Tooltip",
     .get = [](sol::state_view lua, Menu& self) { return sol::make_object(lua, menuNode(lua, self).tooltip); },
     .set = [](sol::state_view lua, Menu& self, const sol::object& value) { menuNode(lua, self).tooltip = textOf(lua, value); }},
    menuFlag<&MenuNode::can_check>("CanCheck"),
    menuFlag<&MenuNode::checked>("Checked"),
    {.name = "OnClick",
     .get = [](sol::state_view lua, Menu& self) -> sol::object {
         const sol::protected_function& click = context(lua).menu_clicks.at(self.index);
         return click.valid() ? sol::object(click) : sol::object(sol::lua_nil);
     }},
    {.name = "Menus",
     .get =
         [](sol::state_view lua, Menu& self) {
             sol::table table = lua.create_table();
             int at = 1;
             for (size_t child : menuNode(lua, self).children) {
                 if (!context(lua).menus[child].removed) table[at++] = pushMenu(lua, child);
             }
             return sol::object(table);
         }},
    // Add(name, onClick[, canCheck, checked[, tooltip]]). A child of the same name, ignoring
    // case, is returned as it is rather than added again.
    {.name = "Add",
     .call =
         [](sol::state_view lua, Menu& self, sol::variadic_args args) {
             Context& ctx = context(lua);
             const std::string name = argString(lua, args, 0, "Add");
             for (size_t child : ctx.menus.at(self.index).children) {
                 if (!ctx.menus[child].removed && sameName(ctx.menus[child].name, name)) return pushMenu(lua, child);
             }

             MenuNode node;
             node.name = name;
             node.can_check = argBool(args, 2);
             node.checked = argBool(args, 3);
             node.tooltip = args.size() > 4 ? textOf(lua, args[4]) : std::string();
             node.parent = self.index;
             const size_t index = ctx.menus.size();
             ctx.menus.push_back(std::move(node));
             ctx.menus[self.index].children.push_back(index);

             const sol::object click = argument(args, 1);
             ctx.menu_clicks.push_back(
                 click.get_type() == sol::type::function ? sol::protected_function(click) : sol::protected_function());
             return pushMenu(lua, index);
         }},
    {.name = "Remove",
     .call =
         [](sol::state_view lua, Menu& self, sol::variadic_args args) {
             const sol::object removed = argument(args, 0);
             if (removed.is<Menu>()) {
                 Context& ctx = context(lua);
                 std::vector<size_t>& children = ctx.menus.at(self.index).children;
                 const size_t index = removed.as<Menu>().index;
                 if (std::find(children.begin(), children.end(), index) != children.end()) ctx.menus.at(index).removed = true;
             }
             return sol::object(sol::lua_nil);
         }},
};

// PathingGlobal's `Packs`: World under its old name, with a warning the first time.
sol::object deprecatedGlobal(sol::this_state state, const sol::table&, const sol::object& key) {
    sol::state_view lua(state);
    if (key.get_type() != sol::type::string || key.as<std::string_view>() != "Packs") return sol::lua_nil;
    Context& ctx = context(state);
    if (!ctx.warned_packs) {
        ctx.console.push(Level::System, "system", "`Packs` is deprecated.  Use `World` instead.");
        ctx.warned_packs = true;
    }
    return ctx.env.raw_get<sol::object>("World");
}

} // namespace

std::span<const Member<Menu>> Menu::members() {
    return kMenuMembers;
}

sol::object pushMenu(sol::state_view lua, size_t index) {
    std::unordered_map<size_t, sol::object>& cache = context(lua).menu_objects;
    auto [it, inserted] = cache.try_emplace(index);
    if (inserted) it->second = sol::make_object(lua, Menu{index});
    return it->second;
}

void bindGlobals(sol::state_view lua, sol::table env) {
    bindType<CurrentMapApi>(lua);
    bindType<PlayerCharacterApi>(lua);
    bindType<PlayerCameraApi>(lua);
    bindType<UiApi>(lua);
    bindType<InfoApi>(lua);
    bindType<MumbleApi>(lua);
    bindType<DebugApi>(lua);
    bindType<EventApi>(lua);
    bindType<StorageApi>(lua);
    bindType<UserApi>(lua);
    bindType<PathingApi>(lua);
    bindType<InstanceApi>(lua);
    bindType<PackContext>(lua);
    bindType<Menu>(lua);

    env["Mumble"] = MumbleApi{};
    env["Debug"] = DebugApi{};
    env["Event"] = EventApi{};
    env["Storage"] = StorageApi{};
    env["User"] = UserApi{};
    env["Pathing"] = PathingApi{};
    env["PathingVersion"] = kPathingVersion;
    env["I"] = InstanceApi{};

    Context& ctx = context(lua);
    ctx.menus.push_back(MenuNode{.name = "Scripts"});
    ctx.menu_clicks.emplace_back();
    env["Menu"] = pushMenu(lua, 0);

    // Scripting/PathingGlobal.cs, "Replicated LuaGlobal": these and no others. Lua's own stand
    // in for NeoLua's wherever the two agree. NeoLua's rawmembers, rawarray, mpairs and
    // nextKey, and its bit32, have no counterpart here; no corpus script calls them.
    for (const char* name :
         {"getmetatable",
          "ipairs",
          "pairs",
          "next",
          "rawequal",
          "rawget",
          "rawlen",
          "rawset",
          "select",
          "setmetatable",
          "tonumber",
          "tostring",
          "type",
          "_VERSION",
          "coroutine",
          "math",
          "string",
          "table"}) {
        env[name] = lua[name];
    }
    env["os"] = makeOsLibrary(lua);

    // `table` is NeoLua's LuaTable, which has LSON besides Lua's own functions.
    sol::table table_library = lua["table"];
    table_library["ToLson"] =
        [](sol::this_state state, const sol::table& table, sol::optional<bool> pretty, sol::optional<std::string> indent) {
            return toLson(state, table, pretty.value_or(true), indent.value_or("\t"));
        };
    table_library["FromLson"] = [](sol::this_state state, const std::string& text) { return fromLson(sol::state_view(state), text); };

    // print(...) is Debug.Print of every argument's text, joined by spaces; nil prints empty.
    env["print"] = [](sol::this_state state, sol::variadic_args args) {
        std::string line;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) line += ' ';
            line += textOf(state, args[i]);
        }
        context(state).console.push(Level::Info, callerLocation(state).source, line);
    };

    // require(name, pack), the global spelling of Pack:Require.
    env["require"] = [](sol::this_state state, sol::variadic_args args) {
        const sol::object pack = argument(args, 1);
        return requireScript(sol::state_view(state), packArgument(state, pack, "require"), argString(state, args, 0, "require"));
    };

    sol::table meta = lua.create_table();
    meta[sol::meta_function::index] = deprecatedGlobal;
    env[sol::metatable_key] = meta;
}

} // namespace viewer::script
