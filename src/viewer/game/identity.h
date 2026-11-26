#pragma once

#include <string>

// MumbleLink's identity: the JSON the game writes into Mumble::Data::Identity.
//
// Nexus does not expose it parsed — there is no DL_MUMBLE_LINK_IDENTITY and Nexus.h mentions
// Identity nowhere — so it is read here, for the camera's field of view and for what scripts
// see of the player.
namespace viewer {

// The game's own field names in comments. Zero, or empty, for whatever the JSON lacks.
struct Identity {
    std::string name; // "name", UTF-8
    int profession = 0; // "profession", Mumble::EProfession
    int specialization = 0; // "spec"
    int race = 0; // "race", Mumble::ERace
    int map_id = 0; // "map_id"
    int world_id = 0; // "world_id"
    int team_color_id = 0; // "team_color_id"
    bool commander = false; // "commander"
    float fov = 0.0f; // "fov", radians
    int ui_size = 1; // "uisz", Mumble::EUIScale; Normal when absent
};

// The identity in `json`. The last text parsed is kept, so calling this every frame with
// the same text costs a comparison: the game rewrites it only when something in it changes.
// Malformed or missing text is the empty identity; it never throws.
const Identity& parseIdentity(const wchar_t* json);

} // namespace viewer
