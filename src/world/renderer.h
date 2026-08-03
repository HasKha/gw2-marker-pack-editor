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

} // namespace world::renderer
