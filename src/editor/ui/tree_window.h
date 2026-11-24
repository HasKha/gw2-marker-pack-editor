#pragma once

#include <string>

#include "editor/editor_state.h"

namespace ui::tree_window {

// Takes the pending reveal off State and works out which categories to open for it. Runs every
// frame, drawn or not, so a reveal is dropped rather than replayed when the tree comes back. Call
// after anything that can request one, before draw().
void update(editor::State& state);

// Draws the category hierarchy into the current window, revealing whatever update() worked out.
void draw(editor::State& state);

// The node's full dotted category path, e.g. "haskha.arah.shogg": how a node is named away from
// the tree.
std::string nodePath(const pack::Pack& pack, editor::NodeRef node);

} // namespace ui::tree_window
