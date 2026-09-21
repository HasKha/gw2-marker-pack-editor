#pragma once

#include "editor/editor_state.h"
#include "editor/world/camera.h"

namespace world::renderer {

// Where a POI sits and how it is oriented, resolved through the category chain exactly as
// draw() resolves it.
//
// It lives here, and the gizmo calls it, because the two have to agree to the last digit:
// the gizmo's handles are supposed to sit on the drawn icon, and a second copy of this
// logic is a second chance to disagree with the picture. (There were two, and they did —
// one accepted a partial rotate="90" the way Blish does, the other required all three
// components and silently read it as no rotation at all.)
struct MarkerTransform {
    glm::vec3 position{0.0f}; // as drawn, i.e. with heightOffset already applied
    glm::vec3 rotation{0.0f}; // radians, in the attributes' own axis order
    float height_offset = 0.0f;

    // Any of rotate / rotate-x / rotate-y / rotate-z turns the marker from a billboard
    // into a fixed-orientation quad — including rotate="0,0,0", which is not a no-op.
    bool has_rotation = false;
};

// False for anything that cannot be placed: a trail, or a POI missing a coordinate. Those
// are invisible anchors, and the loader drops them too.
bool markerTransform(const pack::Pack& pack, const pack::Pathable& pathable, MarkerTransform* out);

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
