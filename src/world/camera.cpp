#include "world/camera.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>

namespace world {

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

bool Camera::project(const glm::vec3& world, glm::vec2* out_screen) const {
    const glm::vec4 clip = view_projection * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0001f) return false;

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;

    // NDC y points up, screen y points down.
    out_screen->x = (ndc.x * 0.5f + 0.5f) * viewport.x;
    out_screen->y = (0.5f - ndc.y * 0.5f) * viewport.y;
    return true;
}

float Camera::pixelSize(float world_size, float distance) const {
    if (distance <= 0.0001f) return 0.0f;
    // Standard perspective scale: half the viewport spans tan(fov/2) at unit distance.
    return world_size / distance * (viewport.y * 0.5f) / std::tan(fov * 0.5f);
}

Camera fromMumble(const Mumble::Data* mumble, const NexusLinkData* nexus_link) {
    Camera camera;

    if (!mumble || !nexus_link) return camera;
    if (mumble->UITick == 0) return camera; // game has not written anything yet
    if (nexus_link->Width == 0 || nexus_link->Height == 0) return camera;

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

    camera.viewport = glm::vec2(static_cast<float>(nexus_link->Width), static_cast<float>(nexus_link->Height));
    camera.aspect = camera.viewport.x / camera.viewport.y;
    camera.fov = parseFieldOfView(mumble->Identity, camera.fov);

    // Left-handed to match Mumble's coordinate system.
    camera.view = glm::lookAtLH(camera.position, camera.position + camera.front, camera.up);
    camera.projection = glm::perspectiveLH(camera.fov, camera.aspect, 0.1f, 10000.0f);
    camera.view_projection = camera.projection * camera.view;

    camera.valid = true;
    return camera;
}

} // namespace world
