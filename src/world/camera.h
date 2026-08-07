#pragma once

#include <Mumble.h>
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
    //
    // `out_w` optionally receives the clip-space w, i.e. the view-space depth the position
    // was divided by. It is written even when the point is behind the camera. Anything
    // interpolating across a projected quad needs it, because the screen positions alone
    // no longer say how the surface between them is foreshortened.
    bool project(const glm::vec3& world, glm::vec2* out_screen, float* out_w = nullptr) const;

    // The four world-space corners of the camera-facing quad Blish draws for a marker
    // `world_size` across, centred on `world_position`, in kQuadCorners order. This is
    // billboardCorners before the projection, for callers that have to subdivide the quad
    // in world space rather than on screen.
    bool billboardQuad(const glm::vec3& world_position, float world_size, glm::vec3 out_corners[4]) const;

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

// The most ways a quad is ever split. The renderer sizes a stack grid on this, so it is a
// bound rather than a preference.
constexpr int kMaxQuadSubdivisions = 16;

// How many ways to split a projected quad along each axis before interpolating across it
// *affinely* is worth less than `tolerance_pixels` of error.
//
// A 2D draw list has no w to divide by, so anything it interpolates across a quad — the
// texture coordinates, above all — is spread evenly over the screen positions rather than
// over the surface. That is only right when the four corners came back from the projection
// at the same depth, and a quad tilted away from the camera does not.
//
// Across an edge whose ends projected with clip depths w0 and w1, the affine midpoint lands
// at parameter 1/2 where the correct one is w0/(w0+w1) — so the content is misplaced by a
// fraction of the edge's screen length. Splitting the edge n ways divides both that
// fraction and the length it is measured over by n, so the error falls as 1/n².
//
// The depth spread is measured against the *near* end rather than the mean, deliberately.
// The error is not spread evenly over the quad: it concentrates at the near corner, where
// the cells come out widest on screen and shallowest in depth. Dividing by the mean sizes
// the grid for the average cell and leaves that corner several times over tolerance.
//
// Returns 1 — draw the quad whole — when there is nothing to correct: a quad square on to
// the camera, or one with a corner behind it, where the projection means nothing anyway.
// Never more than kMaxQuadSubdivisions.
int affineQuadSubdivisions(const glm::vec2 screen[4], const float depth[4], float tolerance_pixels);

// Builds the camera for this frame. Returns an invalid Camera when MumbleLink has no data
// yet — at character select, during map load, or before the game has ticked.
//
// `viewport` is the size in pixels of the surface everything will be *drawn* into, which
// is ImGui's display size and nothing else. NexusLinkData carries a Width/Height too, but
// those come from the DXGI swap chain and Nexus only refreshes them from its ResizeBuffers
// hook — a swap chain recreated at another resolution leaves them stale. Projecting with
// one shape and rasterizing into another stretches the whole world along one axis: markers
// come out squashed and the gizmo's circles come out as ellipses.
Camera fromMumble(const Mumble::Data* mumble, const glm::vec2& viewport);

} // namespace world
