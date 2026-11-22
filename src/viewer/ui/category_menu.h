#pragma once

#include <string>

#include "viewer/registry.h"
#include "viewer/store.h"

// The window that turns categories on and off: every installed pack's categories, merged.
namespace viewer::ui::category_menu {

// Draws the window while `*open`, its close button clearing it. `status` is a line above the
// tree — how the loading is going. True when a category was turned on or off this frame.
bool draw(const registry::CategoryTree& tree, store::CategoryStates* states, bool* open, const std::string& status);

} // namespace viewer::ui::category_menu
