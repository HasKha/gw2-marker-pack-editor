#pragma once

#include <imgui.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "viewer/game/camera.h"
#include "viewer/render/scene.h"

// World-space quads drawn into an ImGui draw list: the sink used when GoodRender is not
// served, and what the editor's overlay projects its outlines with.
//
// A draw list is two triangles with affine texture coordinates and no depth, so two things
// the GPU would do for free are done here instead: clipping against the near plane, and
// perspective-correct texturing by subdividing in world space.
namespace viewer::overlay {

// A world-space quad clipped against the near plane, ready to draw and to click. Never culled on
// its centre or a corner: a large marker you stand inside would vanish while still on screen.
struct ClippedQuad {
    glm::vec3 world[4]; // kQuadCorners order
    // How far each corner is in front of the near plane, negative behind it. Kept because
    // every cell of a subdivision re-clips against them.
    float corner_depth[4] = {};

    int count = 0; // corners that survived the clip: 0 when none of the quad is in front, else 3..5
    glm::vec2 screen[5];
    float depth[5] = {}; // the clip w each surviving corner was divided by
};

// Clips `world` against the near plane and projects what survives. False when no part of
// the quad is in front of the camera.
bool clipAndProject(const Camera& camera, const glm::vec3 world[4], ClippedQuad* out);

// The visible corners in ImGui's type, for outlines, fills and hit tests. Returns the count.
int screenCorners(const ClippedQuad& quad, ImVec2 out[5]);

// Draws the visible part of a textured quad, subdividing as far as the perspective across it
// needs and clipping each cell that straddles the near plane. `uv` is per corner of the whole
// quad, in kQuadCorners order.
//
// A quad square on to the camera and wholly in front of it is one AddImageQuad, which is the
// common case by a long way. Anything else is one draw command for the whole grid.
void drawTexturedQuad(
    ImDrawList* list,
    const Camera& camera,
    ImTextureID texture,
    const ClippedQuad& quad,
    const ImVec2 uv[4],
    ImU32 colour);

// Draws `scene` into `list`, projected with `camera`, in the scene's order: the items something of
// the world would hide when `occluded`, the others when `over`. A draw list has no depth, so
// nothing is hidden. The sink without GoodRender, and GoodRender's fallback over the world.
void drawScene(ImDrawList* list, const Camera& camera, const Scene& scene, bool occluded = true, bool over = true);

} // namespace viewer::overlay
