#pragma once

#include <imgui.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "viewer/camera.h"
#include "viewer/scene.h"

// World-space quads drawn into an ImGui draw list: the sink used when GoodRender is not
// served, and what the editor's overlay projects its outlines with.
//
// A draw list is two triangles with affine texture coordinates and no depth, so two things
// the GPU would do for free are done here instead: clipping against the near plane, and
// perspective-correct texturing by subdividing in world space.
namespace viewer::overlay {

// A world-space quad once the near plane has had it, ready to draw and to click.
//
// A quad only ever gets drawn through this, because a marker large enough to matter is
// routinely one you are standing inside: its centre goes behind you while most of it is
// still on screen. Dropping a quad because a corner — or the centre — is behind the camera
// is a marker that vanishes as you walk up to it. Blish never faces the question; the GPU
// clips for it.
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

// Draws `scene` into `list`, projected with `camera`, in the scene's order: the items something
// of the world hides when `occluded`, the others when `over`. Hidden behind nothing, as a draw
// list has no depth. The sink when no GoodRender service draws the frame, over everything, and GoodRender's
// own for what goes over the world.
void drawScene(ImDrawList* list, const Camera& camera, const Scene& scene, bool occluded = true, bool over = true);

} // namespace viewer::overlay
