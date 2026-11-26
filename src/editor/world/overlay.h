#pragma once

#include "editor/editor_state.h"
#include "viewer/game/camera.h"
#include "viewer/render/scene.h"

// What only an editor draws over the world, the viewer having drawn the pack itself: the
// outline of what is selected and hovered, the open trail's point handles, and where an icon or
// a trail's texture is missing. Picking comes from the same pass.
namespace world::overlay {

// Draws over `scene`, the viewer's scene this frame, under `camera`. The open pack's items in
// it carry their pathable, so the outlines and the picking go by the very shapes the viewer
// drew: what you click is exactly what you see, with no separate ray cast that could disagree
// with the picture.
void draw(editor::State& state, const viewer::Camera& camera, const viewer::Scene& scene);

// Nothing is under the mouse: for a frame the overlay is not drawn — the map is up, or the
// world is not on screen — so a click cannot select what was under it before.
void clearHovered();

// Index into Pack::pathables of the marker under the mouse, or pack::kNoIndex. Valid until
// the next draw(); read from the game's message thread.
size_t hoveredPathable();

// Flat index of the trail point under the mouse, or pack::kNoIndex. Only points of the trail
// open in the trail editor are drawn as handles, so only those can be picked; the index is
// into that trail. Valid until the next draw().
size_t hoveredTrailPoint();

} // namespace world::overlay
