#pragma once

#include "editor/editor_state.h"

namespace ui::property_panel {

// Draws the attribute form for the current selection into the current window. Every
// widget is chosen from the attribute table's ValueKind — no attribute is named here.
void draw(editor::State& state);

} // namespace ui::property_panel
