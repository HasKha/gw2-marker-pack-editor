#pragma once

#include "editor/editor_state.h"
#include "editor/world/camera.h"

namespace world::gizmo {

// Draws and applies the 3D manipulator for the current selection.
//
// Only a single selected POI is manipulable: trails need their points edited individually
// (not yet implemented) and a category has no transform of its own.
//
// Returns true while the gizmo is hovered or being dragged, so the caller can stop a click
// from also selecting whatever is behind it.
bool draw(editor::State& state, const Camera& camera);

} // namespace world::gizmo
