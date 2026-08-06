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

// Corner order for every marker quad, as multipliers on the quad's own right and up axes.
// Blish's marker UVs are a function of the sign of each vertex, so the order a quad's
// corners come out in is what its UV table has to be written against — the two only line
// up because everything drawing a marker uses this one array.
constexpr glm::vec2 kQuadCorners[4] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};

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

    // The full map is up. The camera keeps updating behind it, so nothing else notices,
    // but drawing markers over the map is drawing them in the wrong place entirely.
    bool map_open = false;

    // Projects to screen pixels. Returns false when the point is behind the camera, in
    // which case the screen position is meaningless.
    bool project(const glm::vec3& world, glm::vec2* out_screen) const;

    // Projects the camera-facing quad Blish draws for a marker `world_size` across,
    // centred on `world_position`, into screen pixels in kQuadCorners order.
    //
    // The whole quad is projected rather than its size being computed from the distance,
    // because neither its size nor its shape follows from one. A billboard is
    // perpendicular to the *ray* to the camera, so how tall it projects depends on the
    // view-space depth and how far off-axis it sits, not on the radial distance — over
    // 1.6x apart by the middle of the screen edge at a 70-degree vertical FOV. And what
    // lands on screen is a tilted quad, not an axis-aligned square. Blish projects the
    // four corners and uses what comes out (StandardMarker.World.cs), so we do too.
    //
    // Returns false when a corner falls behind the camera. That only happens with the
    // marker in the camera's face, where Blish's own screen bounds are meaningless too.
    bool billboardCorners(const glm::vec3& world_position, float world_size, glm::vec2 out_corners[4]) const;
};

// The screen-space height of four projected corners. This — not the width, and not the
// diagonal — is the quantity Blish's minSize/maxSize clamp is expressed in.
float quadPixelHeight(const glm::vec2 corners[4]);

// Builds the camera for this frame. Returns an invalid Camera when MumbleLink has no data
// yet — at character select, during map load, or before the game has ticked.
Camera fromMumble(const Mumble::Data* mumble, const NexusLinkData* nexus_link);

} // namespace world
