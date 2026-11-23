#include "viewer/script/mumble.h"

#include <Mumble.h>

#include "viewer/identity.h"

namespace viewer::script {

namespace {

// Gw2Sharp's enum names (Gw2Sharp/Models/*.cs), in MumbleLink's numbering, which both follow.
// A gap is a value Gw2Sharp has no name for.
constexpr const char* kMapTypes[] = {
    "Redirect",   "CharacterCreate", "Pvp",        "Gvg",      "Instance",  "Public",  "Tournament",
    "Tutorial",   "UserTournament",  "Center",     "BlueHome", "GreenHome", "RedHome", "FortunesVale",
    "JumpPuzzle", "EdgeOfTheMists",  "PublicMini", nullptr,    "WvwLounge",
};
constexpr const char* kProfessions[] = {
    nullptr,
    "Guardian",
    "Warrior",
    "Engineer",
    "Ranger",
    "Thief",
    "Elementalist",
    "Mesmer",
    "Necromancer",
    "Revenant",
};
constexpr const char* kRaces[] = {"Asura", "Charr", "Human", "Norn", "Sylvari"};
constexpr const char* kMounts[] = {
    "None",
    "Jackal",
    "Griffon",
    "Springer",
    "Skimmer",
    "Raptor",
    "RollerBeetle",
    "Warclaw",
    "Skyscale",
    "Skiff",
    "SiegeTurtle",
};
constexpr const char* kUiSizes[] = {"Small", "Normal", "Large", "Larger"};

// A value the enum has no name for is its number, which is what .NET prints for one.
template <size_t N>
std::string nameOf(const char* const (&names)[N], int value) {
    const bool named = value >= 0 && static_cast<size_t>(value) < N && names[value];
    return named ? names[value] : std::to_string(value);
}

glm::vec3 toBlish(const Vector3& v) {
    return glm::vec3(v.X, v.Z, v.Y);
}

} // namespace

MumbleState mumbleState(const Mumble::Data* data) {
    MumbleState state;
    if (!data || data->UITick == 0) return state;

    const Mumble::Context& context = data->Context;
    const Identity& identity = parseIdentity(data->Identity);

    state.available = true;
    state.tick = static_cast<int>(data->UITick);

    state.map_id = static_cast<int>(context.MapID);
    state.map_type = nameOf(kMapTypes, static_cast<int>(context.MapType));
    state.is_competitive = context.IsCompetitive;

    state.player_position = toBlish(data->AvatarPosition);
    state.player_forward = toBlish(data->AvatarFront);
    state.name = identity.name;
    state.profession = nameOf(kProfessions, identity.profession);
    state.race = nameOf(kRaces, identity.race);
    state.specialization = identity.specialization;
    state.team_color_id = identity.team_color_id;
    state.is_commander = identity.commander;
    state.is_in_combat = context.IsInCombat;
    state.mount = nameOf(kMounts, static_cast<int>(context.MountIndex));

    state.camera_position = toBlish(data->CameraPosition);
    state.camera_forward = toBlish(data->CameraFront);
    if (identity.fov > 0.0f) state.field_of_view = identity.fov;

    state.compass_width = context.Compass.Width;
    state.compass_height = context.Compass.Height;
    state.compass_rotation = context.Compass.Rotation;
    state.is_compass_rotation_enabled = context.IsCompassRotating;
    state.is_compass_top_right = context.IsCompassTopRight;
    state.is_map_open = context.IsMapOpen;
    state.map_scale = context.Compass.Scale;
    state.is_text_input_focused = context.IsTextboxFocused;
    state.ui_size = nameOf(kUiSizes, identity.ui_size);

    state.build_id = static_cast<int>(context.BuildID);
    state.is_game_focused = context.IsGameFocused;
    state.shard_id = context.ShardID;
    state.process_id = context.ProcessID;
    return state;
}

} // namespace viewer::script
