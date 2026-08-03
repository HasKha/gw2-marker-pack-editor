#pragma once

#include "editor_state.h"

namespace ui::tree_window {

// Draws the category hierarchy into the current window. Categories are the tree; the
// XML files a pack is split across are an authoring convenience and do not appear as
// nodes. See docs/architecture.md for why.
void draw(editor::State& state);

} // namespace ui::tree_window
