#pragma once

#include <Mumble.h>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace viewer {

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

// The weights of a quad's four corners at a point in the quad's own (u,v) parameter space:
// u runs from corner 0 to corner 1 and v from corner 0 to corner 3, both over 0..1, so the
// corners themselves sit at (0,0), (1,0), (1,1), (0,1) — kQuadCorners order.
//
// Every quantity a quad carries is this one blend of its corner values: the world position,
// the view depth, the texture coordinate. That is what makes (u,v) enough on its own to
// clip a quad and to subdivide it, whatever else is riding along.
inline void bilinearWeights(glm::vec2 at, float out_weight[4]) {
    out_weight[0] = (1.0f - at.x) * (1.0f - at.y);
    out_weight[1] = at.x * (1.0f - at.y);
    out_weight[2] = at.x * at.y;
    out_weight[3] = (1.0f - at.x) * at.y;
}

inline glm::vec3 bilinearPoint(const glm::vec3 corner[4], glm::vec2 at) {
    float weight[4];
    bilinearWeights(at, weight);
    return corner[0] * weight[0] + corner[1] * weight[1] + corner[2] * weight[2] + corner[3] * weight[3];
}

inline float bilinearValue(const float corner[4], glm::vec2 at) {
    float weight[4];
    bilinearWeights(at, weight);
    return corner[0] * weight[0] + corner[1] * weight[1] + corner[2] * weight[2] + corner[3] * weight[3];
}

// Clips the axis-aligned region `low`..`high` of a quad's (u,v) space to the part in front
// of the near plane, given the quad's four corner depths relative to that plane. Writes the
// corners of what survives, in order, and returns how many: 0 when none of it is in front,
// 4 when all of it is, and 3 or 5 when the plane cuts across. One plane can only ever add
// one corner.
//
// Working in (u,v) is what keeps this exact. Depth is a bilinear blend of the corner
// depths, which is *linear* along an axis-aligned edge — and the only edges ever cut here
// are axis-aligned, since the input is a rectangle and the plane is applied once.
int clipQuadRegion(const float corner_depth[4], glm::vec2 low, glm::vec2 high, glm::vec2 out_corners[5]);

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
    float near_plane = 0.1f; // metres; what the projection below was built with
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

    // How far each of a quad's corners is in front of the near plane — negative behind it.
    // This is the clip w that project() divides by, less near_plane, and it is what
    // clipQuadRegion wants.
    void quadNearDepths(const glm::vec3 world[4], float out_depth[4]) const;
};

// The screen-space height of a projected quad or polygon. This — not the width, and not the
// diagonal — is the quantity Blish's minSize/maxSize clamp is expressed in.
float quadPixelHeight(const glm::vec2* points, int count = 4);

// The size across, in metres, to draw a billboard `world_size` across centred on `centre` so
// that its projected height obeys minSize and maxSize the way Blish's does. Returns 0 when it
// would come out under a pixel tall: not worth drawing.
//
// Blish clamps by rescaling the model matrix about the marker, so the quad is rebuilt at the
// new size rather than stretched, or its shape would not be Blish's — which is why this
// returns a size and not a screen rectangle.
//
// A billboard straddling the near plane has no projected height to measure and keeps its
// true size. Blish's own screen bounds are meaningless there too, and inventing a height
// would resize a marker for walking into it.
float clampedBillboardSize(const Camera& camera, const glm::vec3& centre, float world_size, float min_size, float max_size);

// The most ways a quad is ever split. The overlay sizes a stack grid on this, so it is a
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
// Returns 1 — draw the shape whole — when there is nothing to correct: a quad square on to
// the camera, or one with a corner behind it, where the projection means nothing anyway.
// Never more than kMaxQuadSubdivisions.
//
// `count` is 4 for a whole quad and up to 5 for one the near plane has cut.
int affineQuadSubdivisions(const glm::vec2* screen, const float* depth, int count, float tolerance_pixels);

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

} // namespace viewer
