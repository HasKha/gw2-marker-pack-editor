#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace Mumble {
struct Data;
}

// MumbleLink as Blish HUD's Gw2MumbleService presents it, read once per frame so every script
// and every behavior sees the same frame; tests fill one in by hand.
//
// It is the script API's `Mumble` as it is — PathingGlobal hands scripts Blish HUD's own
// service, so its names, units and types are the API, not MumbleLink's — and what behaviors
// filter on.
namespace viewer {

// Gw2Sharp's enums (Gw2Sharp/Models/*.cs), which number their values as MumbleLink does.
enum class Gw2Enum : uint8_t { MapType, Profession, Race, Mount, UiSize };

// `value`'s name, or its number where the enum has none: what .NET prints for either, and so
// what a script that concatenates or tostring()s one gets in Pathing.
std::string enumName(Gw2Enum type, int value);

// The value named `name`, ignoring case, or 0 for a name the enum does not have — Pathing's
// EnumUtil.TryParseCacheEnum and its default(T). Numbers are not names: "5" is 0 as well.
int enumValue(Gw2Enum type, std::string_view name);

struct MumbleState {
    bool available = false;
    int tick = 0;

    // CurrentMap
    int map_id = 0;
    int map_type = 0; // Gw2Enum::MapType
    bool is_competitive = false;

    // PlayerCharacter. Positions are Blish's: metres, Z up — MumbleLink's (x, z, y), as
    // Vector3Extensions.ToXnaVector3 swaps them.
    glm::vec3 player_position{0.0f};
    glm::vec3 player_forward{0.0f, 0.0f, -1.0f}; // XNA's Vector3.Forward until the game says
    std::string name;
    int profession = 0; // Gw2Enum::Profession
    int race = 0; // Gw2Enum::Race
    int specialization = 0;
    int team_color_id = 0;
    bool is_commander = false;
    bool is_in_combat = false;
    int mount = 0; // Gw2Enum::Mount

    // PlayerCamera
    glm::vec3 camera_position{0.0f};
    glm::vec3 camera_forward{0.0f, 0.0f, -1.0f};
    float field_of_view = 1.222f; // radians; the game's default until the identity says

    // UI
    int compass_width = 0;
    int compass_height = 0;
    double compass_rotation = 0.0;
    bool is_compass_rotation_enabled = false;
    bool is_compass_top_right = false;
    bool is_map_open = false;
    double map_scale = 1.0;
    bool is_text_input_focused = false;
    int ui_size = 1; // Gw2Enum::UiSize: Normal

    // Info
    int build_id = 0;
    bool is_game_focused = false;
    uint32_t shard_id = 0;
    uint32_t process_id = 0;
};

// The frame MumbleLink holds now. Not available while the game has not written it.
MumbleState mumbleState(const Mumble::Data* data);

} // namespace viewer
