#pragma once

#include "editor_state.h"
#include "world/camera.h"

namespace world::renderer {

// Draws the open pack into the world for this frame. Safe to call with an invalid camera
// or no pack; it simply draws nothing.
//
// What gets drawn is controlled per node by the visibility toggles in the tree, not by
// global switches.
void draw(editor::State& state, const Camera& camera);

// Index into Pack::pathables of whatever is under the mouse, or pack::kNoIndex.
//
// Computed during draw() from the very screen rectangles the markers were drawn with, so
// what you click is exactly what you see — no separate ray cast that could disagree with
// the picture. Valid until the next draw().
size_t hoveredPathable();

// Flat index of the trail point under the mouse, or pack::kNoIndex. Only points of the
// trail open in the trail editor are drawn as handles, so only those can be picked; the
// index is into that trail. Valid until the next draw().
size_t hoveredTrailPoint();

} // namespace world::renderer
