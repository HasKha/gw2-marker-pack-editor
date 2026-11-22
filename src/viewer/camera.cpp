#include "viewer/camera.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {

namespace {

glm::vec3 toGlm(const Vector3& v) {
    return glm::vec3(v.X, v.Y, v.Z);
}

bool isFinite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Nexus does not expose a parsed Mumble identity — there is no DL_MUMBLE_LINK_IDENTITY
// and Nexus.h mentions Identity nowhere. So the FOV has to come from the raw identity
// JSON that the game writes into Mumble::Data::Identity.
//
// Reparsing that every frame would be wasteful, so the last-seen string is cached and the
// JSON is only touched when the game changes it.
float parseFieldOfView(const wchar_t* identity, float fallback) {
    static std::wstring cached_identity;
    static float cached_fov = 0.0f;

    if (!identity || identity[0] == L'\0') return fallback;

    if (cached_identity == identity) return cached_fov > 0.0f ? cached_fov : fallback;

    cached_identity = identity;
    cached_fov = 0.0f;

    // The identity JSON is ASCII in practice apart from the character name, and we only
    // need a number out of it, so a narrowing copy is enough.
    std::string narrow;
    narrow.reserve(cached_identity.size());
    for (wchar_t c : cached_identity) narrow.push_back(c < 128 ? static_cast<char>(c) : '?');

    // Malformed identity must not take the addon down; the caller's fallback is fine.
    nlohmann::json parsed = nlohmann::json::parse(narrow, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return fallback;

    auto it = parsed.find("fov");
    if (it != parsed.end() && it->is_number()) cached_fov = it->get<float>();

    return cached_fov > 0.0f ? cached_fov : fallback;
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

    // Matrix.CreateBillboard's basis, which Blish builds with the object-minus-camera
    // direction, right = up x direction and up' = direction x right. Its up vector is
    // (0,0,1) in Blish's Z-up space, i.e. ours below.
    //
    // The formula is copied verbatim, but the space is not: Blish's is right-handed and
    // Mumble's is left-handed, so the `right` that comes out points the opposite way —
    // screen right here, screen left there. That is not a bug to correct. It is what
    // cancels the horizontal half of Blish's inverted UVs, and the renderer's UV table
    // depends on it. See kBillboardUv in renderer.cpp.
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

int affineQuadSubdivisions(const glm::vec2* screen, const float* depth, int count, float tolerance_pixels) {
    if (count < 3 || tolerance_pixels <= 0.0f) return 1;

    float near_depth = depth[0];
    float far_depth = depth[0];
    for (int i = 1; i < count; ++i) {
        near_depth = std::min(near_depth, depth[i]);
        far_depth = std::max(far_depth, depth[i]);
    }
    if (near_depth <= 0.0f) return 1;

    const float skew = (far_depth - near_depth) / near_depth;
    if (skew < 0.001f) return 1;

    float extent = 0.0f;
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            extent = std::max(extent, std::abs(screen[i].x - screen[j].x));
            extent = std::max(extent, std::abs(screen[i].y - screen[j].y));
        }
    }

    const int steps = static_cast<int>(std::ceil(std::sqrt(extent * skew / (2.0f * tolerance_pixels))));
    return std::clamp(steps, 1, kMaxQuadSubdivisions);
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
    camera.fov = parseFieldOfView(mumble->Identity, camera.fov);

    // Left-handed to match Mumble's coordinate system.
    camera.view = glm::lookAtLH(camera.position, camera.position + camera.front, camera.up);
    camera.projection = glm::perspectiveLH(camera.fov, camera.aspect, camera.near_plane, 10000.0f);
    camera.view_projection = camera.projection * camera.view;

    camera.valid = true;
    return camera;
}

} // namespace viewer
