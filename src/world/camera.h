#pragma once

#include <Mumble.h>
#include <Nexus.h>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace world {

// Metres per inch. Marker positions are in metres; fadeNear, fadeFar, triggerRange and
// friends are in inches. Pathing's marker.hlsl divides by exactly this before comparing.
constexpr float kMetresPerInch = 0.0254f;

inline float metresToInches(float metres) {
    return metres / kMetresPerInch;
}
inline float inchesToMetres(float inches) {
    return inches * kMetresPerInch;
}

// The game camera for this frame, derived from MumbleLink.
//
// Mumble's coordinate system is left-handed with X right, Y up, Z forward, in metres —
// the same space marker xpos/ypos/zpos live in, so no conversion is needed. (Blish HUD's
// internal Z-up space is its own business and does not appear in the file format.)
struct Camera {
    bool valid = false;

    glm::vec3 position{0.0f};
    glm::vec3 front{0.0f, 0.0f, 1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 player{0.0f};

    float fov = 1.222f; // vertical, radians
    float aspect = 16.0f / 9.0f;
    glm::vec2 viewport{0.0f};

    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 view_projection{1.0f};

    uint32_t map_id = 0;

    // Projects to screen pixels. Returns false when the point is behind the camera, in
    // which case the screen position is meaningless.
    bool project(const glm::vec3& world, glm::vec2* out_screen) const;

    // How many pixels tall a world-space size appears at the given distance.
    float pixelSize(float world_size, float distance) const;
};

// Builds the camera for this frame. Returns an invalid Camera when MumbleLink has no data
// yet — at character select, during map load, or before the game has ticked.
Camera fromMumble(const Mumble::Data* mumble, const NexusLinkData* nexus_link);

} // namespace world
