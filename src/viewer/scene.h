#pragma once

#include <imgui.h>

#include <glm/vec3.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "viewer/bake.h"
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

    // Null only for the pack open in the editor, whose markers and trails are kept without
    // their image so the editor can show where they are. The sinks draw nothing for it, as
    // Pathing draws nothing without a texture.
    ImTextureID texture = nullptr;
    ImU32 colour = IM_COL32_WHITE; // tint with every fade already in its alpha

    glm::vec3 centre{0.0f};
    float size = 0.0f; // metres across
    float min_size = 0.0f; // minSize / maxSize, as the pack gives them
    float max_size = 0.0f;

    glm::vec3 corners[4];
    ImVec2 uv[4];

    // For a quad its `cull` keeps one face of: drawn only from the side this points to, from
    // corners[0]. Zero for both sides. Settled against the camera it is drawn with, as the
    // corners are; a billboard's always faces the camera, so the scene settles that itself.
    glm::vec3 front{0.0f};

    // What it was sorted by, Blish's DrawOrder: for a marker the squared distance from the
    // camera to where it stands without its heightOffset, rather than where it is drawn; for
    // a piece of trail the largest float, so that every trail is under every marker.
    float depth = 0.0f;

    // With GoodRender, at WORLD_FINISHED and under the game's UI: what of the world in front of
    // it hides it. Without GoodRender nothing does, whatever this says.
    Occlusion occlusion = Occlusion::None;

    // For the pack open in the editor, the pathable this draws, into its Pack::pathables, and
    // whether it is a piece of trail: what the editor outlines and picks. kNoIndex for every
    // other pack's.
    size_t pathable = pack::kNoIndex;
    bool is_trail = false;
};

struct Scene {
    std::vector<SceneItem> items; // back to front: with no depth between our own quads, order is all there is
};

// Pathing's pack settings that change how the world is drawn (ModuleSettings.InitPackSettings),
// with its defaults, and what of this frame they depend on.
struct WorldOptions {
    bool enabled = true; // "Show Markers in World"
    float max_opacity = 1.0f; // "Max Opacity", over every fade
    // "Max View Distance". Pathing measures a marker's distance against it in metres and hands
    // it to the trail shader beside fadeNear and fadeFar, in inches; both are kept.
    float view_distance = 25000.0f;
    float max_trail_speed = 10.0f; // "Max Trail Animation Speed"
    float marker_scale = 1.0f; // "Marker Scale": iconSize and minSize
    bool combat_fade = true; // "Fade Markers and Trails During Combat": half opacity
    bool in_combat = false; // MumbleLink's, this frame
    // Nexus's IsGameplay: false on a loading screen, where MumbleLink keeps its last frame's
    // camera, and IsMapOpen with it, while nothing of the world is on screen.
    bool in_gameplay = true;
    bool show_hidden = false; // what behaviors filter is drawn too, a marker at half opacity
    // Ours: what hides what its pack leaves to the player (no `occlusion`), with GoodRender.
    // The viewer sets it from the saved settings; this is their default.
    Occlusion occlusion = Occlusion::Characters;
};

// The texture for an image of one pack, or null while it is not available — which, for a
// marker, means it is not drawn this frame, as Pathing draws nothing without one.
using TextureLookup = std::function<ImTextureID(size_t pack, const std::string& path)>;

// `lookup`'s image for a pathable's texture, `kept` being its LiveMarker::image or
// LiveTrail::image: what was kept, else looked up, and kept once there unless `edited` says the
// lookup may take it back.
inline ImTextureID keptImage(const TextureLookup& lookup, bool edited, size_t pack, const std::string& path, ImTextureID* kept) {
    if (*kept) return *kept;
    const ImTextureID image = lookup(pack, path);
    if (!edited) *kept = image;
    return image;
}

// Where one of a list of items goes when they are drawn back to front: its depth, larger first, and where it is in the
// list, which breaks a tie, so that equals keep the order they were given in.
struct DepthKey {
    float depth;
    uint32_t index;
};

// `items`' keys in the order to draw them, back to front by `depth`. Keys are sorted rather than the items, which are many
// times larger, and `keys` keeps its memory from one frame to the next.
template <class T>
void backToFront(const std::vector<T>& items, std::vector<DepthKey>* keys) {
    keys->resize(items.size());
    for (size_t i = 0; i < items.size(); ++i) (*keys)[i] = DepthKey{items[i].depth, static_cast<uint32_t>(i)};
    std::sort(keys->begin(), keys->end(), [](const DepthKey& a, const DepthKey& b) {
        return a.depth != b.depth ? a.depth > b.depth : a.index < b.index;
    });
}

// One live map going into the scene.
struct SceneLayer {
    const runtime::LiveMap* live = nullptr;
    const std::vector<char>* visible = nullptr; // runtime::visibleNodes over its nodes
    TextureLookup texture;
    // The pack open in the editor: its items carry their pathable, and keep no texture when
    // their image is missing — see SceneItem. Drawn as every other pack is, so its author sees
    // what a player will. Its images are looked up every frame, as map_scene::Layer::edited's.
    bool edited = false;
};

// Builds the scene from live maps: what is on them, not removed, in game, under a visible
// category and not filtered by its behaviors, every layer's items sorted together. `camera`
// sorts and culls, and its `player` is what fades are measured from; `seconds` is the time
// trail textures scroll by and markers bounce by.
void buildScene(const std::vector<SceneLayer>& layers, const Camera& camera, float seconds, const WorldOptions& options, Scene* out);

// The same over one live map, `visible` being runtime::visibleNodes.
void buildScene(
    const runtime::LiveMap& live,
    const std::vector<char>& visible,
    const Camera& camera,
    float seconds,
    const WorldOptions& options,
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
