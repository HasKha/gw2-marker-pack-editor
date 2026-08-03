#pragma once

#include <string>

#include "editor_state.h"

namespace ui::tree_window {

// Draws the category hierarchy into the current window. Categories are the tree; the
// XML files a pack is split across are an authoring convenience and do not appear as
// nodes. See docs/architecture.md for why.
void draw(editor::State& state);

// The node's full dotted category path, e.g. "haskha.arah.shogg" — a category's `id`, or a
// pathable's `type`. This is how a node is named away from the tree, where the surrounding
// hierarchy is not on screen to give a short name its meaning. Lives here because the tree
// is what decides how a node is named; anything else that has to name one calls this
// rather than growing its own.
std::string nodePath(const pack::Pack& pack, editor::NodeRef node);

} // namespace ui::tree_window
