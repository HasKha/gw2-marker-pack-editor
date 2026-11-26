#pragma once

#include <string>

#include "editor/editor_state.h"

namespace ui::property_panel {

// What the panel keeps between frames.
struct View {
    bool open_delete_popup = false; // the header asked; the popup opens next
    // What the active field held when it was clicked into. One field is active at a time.
    std::string edit_started_value;
    std::string search; // the Add attribute list's filter
};

// Draws the attribute form for the current selection into the current window. Every
// widget is chosen from the attribute table's ValueKind — no attribute is named here.
void draw(editor::State& state, View& view);

} // namespace ui::property_panel
