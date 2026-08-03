#pragma once

#include "editor_state.h"
#include "world/camera.h"

namespace world::renderer {

struct Options {
    bool draw_markers = true;
    bool draw_trails = true;
    bool highlight_selection = true;

    // Draw everything regardless of which map it belongs to. Only useful for debugging
    // the projection against a pack you are not standing in.
    bool ignore_map_filter = false;
};

Options& options();

// Draws the open pack into the world for this frame. Safe to call with an invalid camera
// or no pack; it simply draws nothing.
void draw(editor::State& state, const Camera& camera);

} // namespace world::renderer
