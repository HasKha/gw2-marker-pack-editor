#pragma once

#include <Mumble.h>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace RTAPI {
struct RealTimeData;
}

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

// Corner order for every marker quad, as multipliers on the quad's right and up axes. Blish's UVs
// follow each vertex's sign, so every UV table is written against this one array.
constexpr glm::vec2 kQuadCorners[4] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};

// The weights of a quad's corners at (u,v): u runs from corner 0 to 1, v from 0 to 3, so the
// corners sit at (0,0), (1,0), (1,1), (0,1). Position, depth and texture coordinate are all this
// blend, which is why (u,v) alone is enough to clip and subdivide a quad.
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

// Clips the region `low`..`high` of a quad's (u,v) space to the part in front of the near plane,
// given the corners' depths relative to it. Writes the surviving corners in order and returns how
// many: 0, 3, 4 or 5. Exact because depth is linear along an axis-aligned edge, and only those are
// cut.
int clipQuadRegion(const float corner_depth[4], glm::vec2 low, glm::vec2 high, glm::vec2 out_corners[5]);

// Where a Camera came from.
enum class CameraSource { None, MumbleLink, Rtapi, GoodRender };

// The game camera for this frame, from RTAPI or MumbleLink, or GoodRender's in a moment: left-handed,
// X right, Y up, Z forward, metres, the same space as xpos/ypos/zpos.
struct Camera {
    bool valid = false;
    CameraSource source = CameraSource::None;

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

    bool map_open = false; // the full map is up; the camera keeps updating behind it

    // Projects to screen pixels; false when the point is behind the camera. `out_w` receives the
    // clip-space w, written even then, for anything interpolating across a projected quad.
    bool project(const glm::vec3& world, glm::vec2* out_screen, float* out_w = nullptr) const;

    // The world-space corners of the billboard Blish draws for a marker `world_size` across, in
    // kQuadCorners order: billboardCorners before the projection.
    bool billboardQuad(const glm::vec3& world_position, float world_size, glm::vec3 out_corners[4]) const;

    // The billboard's corners in screen pixels, kQuadCorners order. Projected whole, as Blish does,
    // since neither its size nor its shape follows from the distance (docs/format.md). False when a
    // corner is behind the camera.
    bool billboardCorners(const glm::vec3& world_position, float world_size, glm::vec2 out_corners[4]) const;

    // How far each corner is in front of the near plane, negative behind: clip w less near_plane,
    // as clipQuadRegion wants.
    void quadNearDepths(const glm::vec3 world[4], float out_depth[4]) const;
};

// The screen-space height of a projected polygon: what Blish's minSize/maxSize clamp.
float quadPixelHeight(const glm::vec2* points, int count = 4);

// The size in metres to draw a billboard so its projected height obeys minSize and maxSize as
// Blish's does; 0 under a pixel tall. A size, not a rectangle, because Blish rescales the model
// matrix and the quad keeps its shape. One straddling the near plane keeps its true size.
float clampedBillboardSize(const Camera& camera, const glm::vec3& centre, float world_size, float min_size, float max_size);

// The most ways a quad is ever split along one axis: a bound on the work, which a trail step
// running from far ahead to beside the camera reaches before its error is under a pixel.
constexpr int kMaxQuadSubdivisions = 64;

// Where to cut a quad in (u,v) so that interpolating affinely across each cell errs by less than a
// tolerance.
struct QuadGrid {
    int columns = 1;
    int rows = 1;
    float u[kMaxQuadSubdivisions + 1] = {0.0f, 1.0f};
    float v[kMaxQuadSubdivisions + 1] = {0.0f, 1.0f};
};

// The grid over the region `low`..`high` of `world` that leaves no cell drawn affinely more than
// `tolerance_pixels` from where the perspective puts it, as far as kMaxQuadSubdivisions allows. A
// draw list has no w to divide by, so texture coordinates spread evenly over the screen unless the
// corners share a depth. One cell when there is nothing to correct.
//
// The cuts are not even: a cell `dt` long at depth w errs by about dt²/w³, so every cell errs alike
// when the cuts are even in 1/√w; even cuts make a trail passing the camera a sawtooth. Each axis is
// sized on its own from the error along the region's two edges across it.
QuadGrid perspectiveGrid(const Camera& camera, const glm::vec3 world[4], glm::vec2 low, glm::vec2 high, float tolerance_pixels);

// The (u,v) box round what of `world` is in view, in front of the near plane and inside the
// screen's sides, for perspectiveGrid to cut; false when none is. The sides matter: most of a trail
// step running past the camera is off screen beside it.
bool visibleRegion(const Camera& camera, const glm::vec3 world[4], glm::vec2* low, glm::vec2* high);

// The camera for this frame: RTAPI's while `realtime` is there and holds one, MumbleLink's otherwise;
// invalid while MumbleLink has no data. The map is MumbleLink's either way. `viewport` is ImGui's
// display size, never NexusLinkData's Width/Height: Nexus updates those only from its ResizeBuffers
// hook, so a recreated swap chain leaves them stale and the world comes out stretched.
Camera gameCamera(const Mumble::Data* mumble, const RTAPI::RealTimeData* realtime, const glm::vec2& viewport);

} // namespace viewer
