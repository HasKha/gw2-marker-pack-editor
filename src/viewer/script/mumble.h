#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

namespace Mumble {
struct Data;
}

// What `Mumble` is to a script: Blish HUD's Gw2MumbleService, which PathingGlobal hands over
// as it is — so its names, units and types are the API, not MumbleLink's. Read once per
// frame, so every script sees the same frame; tests fill one in by hand.
namespace viewer::script {

struct MumbleState {
    bool available = false;
    int tick = 0;

    // CurrentMap
    int map_id = 0;
    std::string map_type = "Redirect"; // Gw2Sharp's MapType name
    bool is_competitive = false;

    // PlayerCharacter. Positions are Blish's: metres, Z up — MumbleLink's (x, z, y), as
    // Vector3Extensions.ToXnaVector3 swaps them.
    glm::vec3 player_position{0.0f};
    glm::vec3 player_forward{0.0f, 0.0f, -1.0f}; // XNA's Vector3.Forward until the game says
    std::string name;
    std::string profession = "0"; // enums are their names, which is what a script that
    std::string race = "Asura"; //     concatenates or tostring()s one gets in Pathing
    int specialization = 0;
    int team_color_id = 0;
    bool is_commander = false;
    bool is_in_combat = false;
    std::string mount = "None";

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
    std::string ui_size = "Normal";

    // Info
    int build_id = 0;
    bool is_game_focused = false;
    uint32_t shard_id = 0;
    uint32_t process_id = 0;
};

// The frame MumbleLink holds now. Not available while the game has not written it.
MumbleState mumbleState(const Mumble::Data* data);

} // namespace viewer::script
