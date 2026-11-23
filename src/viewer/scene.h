#pragma once

#include <imgui.h>

#include <glm/vec3.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "viewer/camera.h"
#include "viewer/runtime.h"

// This frame's world: everything that will be drawn, decided at Present and drawn by a sink.
//
// **World space, and no camera.** It is built at one Present and drawn at the next frame's
// GoodRender moments, and projecting it there, with the camera the moment hands over, is what
// keeps that frame of difference from showing. Min/max size needs a marker's size in pixels,
// so it is resolved in the sink for the same reason. Fades are not: they are measured from the
// player, whom no moment knows about.
namespace viewer {

struct SceneItem {
    enum class Kind : uint8_t {
        // A marker turned to the camera: `centre`, `size` across before min/max size.
        Billboard,
        // A quad fixed in the world — a rotated marker, a piece of trail: `corners` and `uv`.
        Quad,
    };
    Kind kind = Kind::Quad;

    ImTextureID texture = nullptr;
    ImU32 colour = IM_COL32_WHITE; // tint with every fade already in its alpha

    glm::vec3 centre{0.0f};
    float size = 0.0f; // metres across
    float min_size = 0.0f; // minSize / maxSize, as the pack gives them
    float max_size = 0.0f;

    glm::vec3 corners[4];
    ImVec2 uv[4];

    float depth = 0.0f; // squared distance from the camera it was sorted with
};

struct Scene {
    std::vector<SceneItem> items; // back to front: with no depth between our own quads, order is all there is
};

// The texture for an image of one pack, or null while it is not available — which, for a
// marker, means it is not drawn this frame, as Pathing draws nothing without one.
using TextureLookup = std::function<ImTextureID(size_t pack, const std::string& path)>;

// Builds the scene from the live map: what is on it, not removed, in game, under a visible
// category and not filtered by its behaviors. `camera` sorts and culls, and its `player` is
// what fades are measured from; `seconds` is the time trail textures scroll by and markers
// bounce by; `visible` is runtime::visibleNodes. With `show_hidden`, what behaviors filter
// is drawn after all — a marker at half opacity, as Pathing draws its ghosts.
void buildScene(
    const runtime::LiveMap& live,
    const std::vector<char>& visible,
    const Camera& camera,
    float seconds,
    bool show_hidden,
    const TextureLookup& texture,
    Scene* out);

// The corners and uvs `item` is drawn with under `camera`: a billboard turned to the camera
// and sized for it by minSize and maxSize, a quad as it is. False when a billboard comes out
// too small to draw. Every sink draws through this, so every sink draws the same shape.
//
// A billboard is Blish's: facing the ray to its own centre, not the camera's plane — see
// Camera::billboardQuad.
bool itemCorners(const Camera& camera, const SceneItem& item, glm::vec3 out_corners[4], const ImVec2** out_uv);

// The scene the sinks draw: one atomic swap of a finished scene. Null until the first is
// published.
void publish(std::shared_ptr<const Scene> scene);
std::shared_ptr<const Scene> published();

} // namespace viewer
