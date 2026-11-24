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

// What of the world in front of a marker or trail hides it, when GoodRender draws it: the
// player's "Marker occlusion", in this order, and GoodRender's HiddenBehind.
enum class Occlusion : uint8_t {
    None, // "None", the default: over the whole world, as Blish HUD draws every marker
    Characters, // "Behind Characters": through walls and hills, never through someone in front of it
    Scene, // "Behind Characters and Terrain": as the world itself is
};

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
    bool show_hidden = false; // what behaviors filter is drawn too, a marker at half opacity
    // Ours: what hides what its pack leaves to the player (RenderLayer::Default), with GoodRender.
    Occlusion occlusion = Occlusion::None;
};

// The texture for an image of one pack, or null while it is not available — which, for a
// marker, means it is not drawn this frame, as Pathing draws nothing without one.
using TextureLookup = std::function<ImTextureID(size_t pack, const std::string& path)>;

// One live map going into the scene.
struct SceneLayer {
    const runtime::LiveMap* live = nullptr;
    const std::vector<char>* visible = nullptr; // runtime::visibleNodes over its nodes
    TextureLookup texture;
    // The pack open in the editor: its items carry their pathable, and keep no texture when
    // their image is missing — see SceneItem. Drawn as every other pack is, so its author sees
    // what a player will.
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
