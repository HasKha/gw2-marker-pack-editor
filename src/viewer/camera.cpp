#include "viewer/camera.h"

#include <algorithm>
#include <cmath>
#include <string>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>

#include "viewer/identity.h"

namespace viewer {

namespace {

glm::vec3 toGlm(const Vector3& v) {
    return glm::vec3(v.X, v.Y, v.Z);
}

bool isFinite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

bool Camera::project(const glm::vec3& world, glm::vec2* out_screen, float* out_w) const {
    const glm::vec4 clip = view_projection * glm::vec4(world, 1.0f);
    if (out_w) *out_w = clip.w;
    if (clip.w <= 0.0001f) return false;

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;

    // NDC y points up, screen y points down.
    out_screen->x = (ndc.x * 0.5f + 0.5f) * viewport.x;
    out_screen->y = (0.5f - ndc.y * 0.5f) * viewport.y;
    return true;
}

bool Camera::billboardQuad(const glm::vec3& world_position, float world_size, glm::vec3 out_corners[4]) const {
    const glm::vec3 from_camera = world_position - position;
    const float distance = glm::length(from_camera);
    if (distance < 0.0001f) return false;

    const glm::vec3 direction = from_camera / distance;

    // Matrix.CreateBillboard's basis: right = up x direction, up' = direction x right, with
    // direction from the camera to the object. In Mumble's left-handed space `right` points the
    // other way from Blish's, which cancels the horizontal half of Blish's inverted UVs; see
    // kBillboardUv in bake.cpp.
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), direction);
    const float right_length = glm::length(right);
    if (right_length < 0.0001f) {
        // Straight above or below the camera, where up x direction degenerates. Blish
        // normalizes a zero vector here and gets NaN; a marker underfoot is better drawn
        // in an arbitrary orientation than not at all.
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        right /= right_length;
    }
    const glm::vec3 up_axis = glm::cross(direction, right);

    const float half = world_size * 0.5f;

    for (int i = 0; i < 4; ++i) {
        out_corners[i] = world_position + right * (kQuadCorners[i].x * half) + up_axis * (kQuadCorners[i].y * half);
    }

    return true;
}

bool Camera::billboardCorners(const glm::vec3& world_position, float world_size, glm::vec2 out_corners[4]) const {
    glm::vec3 world_corners[4];
    if (!billboardQuad(world_position, world_size, world_corners)) return false;

    for (int i = 0; i < 4; ++i) {
        if (!project(world_corners[i], &out_corners[i])) return false;
    }

    return true;
}

void Camera::quadNearDepths(const glm::vec3 world[4], float out_depth[4]) const {
    // `front` is unit length and is the view matrix's +Z, so this is exactly the clip w
    // that project() divides by.
    for (int i = 0; i < 4; ++i) out_depth[i] = glm::dot(world[i] - position, front) - near_plane;
}

float clampedBillboardSize(const Camera& camera, const glm::vec3& centre, float world_size, float min_size, float max_size) {
    // What minSize and maxSize multiply up to, in real pixels. Blish clamps between *4 bounds, but
    // on an NDC extent times the viewport height, which is twice the pixel height (docs/format.md).
    constexpr float kBlishSizeLimit = 2.0f;

    glm::vec2 corners[4];
    if (!camera.billboardCorners(centre, world_size, corners)) return world_size;

    const float measured = quadPixelHeight(corners);
    const float pixels = std::clamp(measured, min_size * kBlishSizeLimit, max_size * kBlishSizeLimit);
    if (pixels < 1.0f) return 0.0f;
    if (measured <= 0.0001f || std::abs(pixels - measured) <= 0.001f) return world_size;
    return world_size * pixels / measured;
}

float quadPixelHeight(const glm::vec2* points, int count) {
    if (count <= 0) return 0.0f;

    float min_y = points[0].y;
    float max_y = points[0].y;

    for (int i = 1; i < count; ++i) {
        min_y = std::min(min_y, points[i].y);
        max_y = std::max(max_y, points[i].y);
    }

    return max_y - min_y;
}

QuadGrid perspectiveGrid(const Camera& camera, const glm::vec3 world[4], glm::vec2 low, glm::vec2 high, float tolerance_pixels) {
    QuadGrid grid;
    grid.u[0] = low.x;
    grid.u[1] = high.x;
    grid.v[0] = low.y;
    grid.v[1] = high.y;
    if (tolerance_pixels <= 0.0f) return grid;

    int* const counts[2] = {&grid.columns, &grid.rows};
    float* const cuts[2] = {grid.u, grid.v};

    // The view depths at the two ends of each axis, along the region's middle line, which
    // place its cuts. Never nearer than the near plane: where the plane cut the quad, corners
    // of the region sit behind it, and none of that is drawn.
    float w_from[2];
    float w_until[2];
    for (int axis = 0; axis < 2; ++axis) {
        glm::vec2 from = (low + high) * 0.5f;
        glm::vec2 until = from;
        from[axis] = low[axis];
        until[axis] = high[axis];
        w_from[axis] = std::max(glm::dot(bilinearPoint(world, from) - camera.position, camera.front), camera.near_plane);
        w_until[axis] = std::max(glm::dot(bilinearPoint(world, until) - camera.position, camera.front), camera.near_plane);
    }

    // Cuts `count` cells along `axis`, evenly in 1/sqrt(w) between the depths at its two ends,
    // which is evenly in the parameter when they are alike.
    const auto placeCuts = [&](int axis, int count) {
        const float from = low[axis];
        const float until = high[axis];
        const float q_from = 1.0f / std::sqrt(w_from[axis]);
        const float q_until = 1.0f / std::sqrt(w_until[axis]);
        const bool level = std::abs(w_until[axis] - w_from[axis]) <= w_from[axis] * 1e-4f;
        for (int i = 0; i <= count; ++i) {
            const float s = static_cast<float>(i) / static_cast<float>(count);
            const float q = q_from + (q_until - q_from) * s;
            const float t = level ? s : (1.0f / (q * q) - w_from[axis]) / (w_until[axis] - w_from[axis]);
            cuts[axis][i] = from + (until - from) * t;
        }
        cuts[axis][0] = from;
        cuts[axis][count] = until;
        *counts[axis] = count;
    };

    // A point of the quad on screen, or false where it is behind the near plane: clipped
    // away, so not measured.
    const auto projectAt = [&](glm::vec2 at, glm::vec2* out) {
        float w = 0.0f;
        return camera.project(bilinearPoint(world, at), out, &w) && w >= camera.near_plane * 0.999f;
    };

    // How far, in pixels, interpolating affinely from `a` to `b` puts their middle from where
    // the perspective does.
    const auto affineError = [&](glm::vec2 a, glm::vec2 b) {
        glm::vec2 start(0.0f), end(0.0f), middle(0.0f);
        const bool seen = projectAt(a, &start) && projectAt(b, &end) && projectAt((a + b) * 0.5f, &middle);
        return seen ? glm::length((start + end) * 0.5f - middle) : 0.0f;
    };

    // The count that takes `error` under the tolerance, when the error falls as 1/count^power.
    const auto nextCount = [&](int count, float error, float power) {
        const int wanted = static_cast<int>(std::ceil(static_cast<float>(count) * std::pow(error / tolerance_pixels, 1.0f / power)));
        return std::clamp(wanted, count + 1, kMaxQuadSubdivisions);
    };

    // First each axis on its own, measured along the region's two edges across it. The error
    // falls about as 1/count^2, so each measurement says how many cells the next try takes; a
    // few tries settle it, since cells that start out large err less than that says.
    constexpr int kTries = 3;
    for (int axis = 0; axis < 2; ++axis) {
        for (int attempt = 0; attempt < kTries; ++attempt) {
            const int count = *counts[axis];
            float worst = 0.0f;
            for (const float across : {low[1 - axis], high[1 - axis]}) {
                for (int i = 0; i < count; ++i) {
                    glm::vec2 a(across);
                    glm::vec2 b(across);
                    a[axis] = cuts[axis][i];
                    b[axis] = cuts[axis][i + 1];
                    worst = std::max(worst, affineError(a, b));
                }
            }
            if (worst <= tolerance_pixels || count == kMaxQuadSubdivisions) break;
            placeCuts(axis, nextCount(count, worst, 2.0f));
        }
    }

    // Then the diagonal each cell is drawn as two triangles across, its first corner to its
    // third. It crosses the same depths as the cell's edges, so a cell wide on screen the
    // other way errs along it by as much more as it is wide — a trail step a metre across,
    // right by the camera, is hundreds of pixels wide with no depth change across it at all.
    // So the cells are split the way the worst of them is widest, its error falling about as
    // that width does.
    for (int attempt = 0; attempt < kTries; ++attempt) {
        float worst = 0.0f;
        float widths[2] = {};
        for (int row = 0; row < grid.rows; ++row) {
            for (int column = 0; column < grid.columns; ++column) {
                const glm::vec2 a(grid.u[column], grid.v[row]);
                const glm::vec2 c(grid.u[column + 1], grid.v[row + 1]);
                const float error = affineError(a, c);
                glm::vec2 corner(0.0f), along_u(0.0f), along_v(0.0f);
                if (error > worst && projectAt(a, &corner) && projectAt(glm::vec2(c.x, a.y), &along_u) &&
                    projectAt(glm::vec2(a.x, c.y), &along_v)) {
                    worst = error;
                    widths[0] = glm::length(along_u - corner);
                    widths[1] = glm::length(along_v - corner);
                }
            }
        }
        if (worst <= tolerance_pixels) break;

        int axis = widths[0] >= widths[1] ? 0 : 1;
        if (*counts[axis] == kMaxQuadSubdivisions) axis = 1 - axis;
        if (*counts[axis] == kMaxQuadSubdivisions) break;
        placeCuts(axis, nextCount(*counts[axis], worst, 1.0f));
    }
    return grid;
}

bool visibleRegion(const Camera& camera, const glm::vec3 world[4], glm::vec2* low, glm::vec2* high) {
    // The near plane, then the four sides of the screen: w ± x and w ± y in clip space, each
    // linear in the world position and so bilinear over the quad, as its depth is.
    float planes[5][4];
    camera.quadNearDepths(world, planes[0]);
    for (int i = 0; i < 4; ++i) {
        const glm::vec4 clip = camera.view_projection * glm::vec4(world[i], 1.0f);
        planes[1][i] = clip.w + clip.x;
        planes[2][i] = clip.w - clip.x;
        planes[3][i] = clip.w + clip.y;
        planes[4][i] = clip.w - clip.y;
    }

    glm::vec2 box_low(0.0f);
    glm::vec2 box_high(1.0f);
    for (const float* plane : planes) {
        glm::vec2 corners[5];
        const int count = clipQuadRegion(plane, box_low, box_high, corners);
        if (count < 3) return false;
        box_low = corners[0];
        box_high = corners[0];
        for (int i = 1; i < count; ++i) {
            box_low = glm::min(box_low, corners[i]);
            box_high = glm::max(box_high, corners[i]);
        }
    }
    *low = box_low;
    *high = box_high;
    return true;
}

int clipQuadRegion(const float corner_depth[4], glm::vec2 low, glm::vec2 high, glm::vec2 out_corners[5]) {
    const glm::vec2 region[4] = {low, glm::vec2(high.x, low.y), high, glm::vec2(low.x, high.y)};

    float depth[4];
    for (int i = 0; i < 4; ++i) depth[i] = bilinearValue(corner_depth, region[i]);

    // Sutherland-Hodgman, one plane: keep every corner in front of it, and add the crossing
    // wherever an edge changes side.
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        const bool inside = depth[i] >= 0.0f;
        const bool next_inside = depth[next] >= 0.0f;

        if (inside) out_corners[count++] = region[i];
        if (inside != next_inside) {
            const float t = depth[i] / (depth[i] - depth[next]);
            out_corners[count++] = region[i] + (region[next] - region[i]) * t;
        }
    }

    return count;
}

Camera fromMumble(const Mumble::Data* mumble, const glm::vec2& viewport) {
    Camera camera;

    if (!mumble) return camera;
    if (mumble->UITick == 0) return camera; // game has not written anything yet
    if (viewport.x < 1.0f || viewport.y < 1.0f) return camera;

    camera.position = toGlm(mumble->CameraPosition);
    camera.front = toGlm(mumble->CameraFront);
    camera.up = toGlm(mumble->CameraTop);
    camera.player = toGlm(mumble->AvatarPosition);
    camera.map_id = mumble->Context.MapID;
    camera.map_open = mumble->Context.IsMapOpen != 0;

    if (!isFinite(camera.position) || !isFinite(camera.front)) return camera;
    if (glm::length(camera.front) < 0.0001f) return camera;

    camera.front = glm::normalize(camera.front);
    // CameraTop is occasionally degenerate; world up is a safe substitute since the game
    // camera does not roll.
    camera.up = glm::length(camera.up) > 0.0001f ? glm::normalize(camera.up) : glm::vec3(0.0f, 1.0f, 0.0f);

    camera.viewport = viewport;
    camera.aspect = camera.viewport.x / camera.viewport.y;
    if (const float fov = parseIdentity(mumble->Identity).fov; fov > 0.0f) camera.fov = fov;

    // Left-handed to match Mumble's coordinate system.
    camera.view = glm::lookAtLH(camera.position, camera.position + camera.front, camera.up);
    camera.projection = glm::perspectiveLH(camera.fov, camera.aspect, camera.near_plane, 10000.0f);
    camera.view_projection = camera.projection * camera.view;

    camera.valid = true;
    return camera;
}

} // namespace viewer
