#pragma once

#include <string>

#include "editor_state.h"

namespace ui::tree_window {

// Takes the pending reveal request off State and works out which categories have to be
// opened to show it. Must run every frame, drawn or not: a window that is hidden — or
// merely collapsed — draws nothing, and a request nobody consumed would sit there until the
// tree came back and then scroll to a node picked minutes ago. Consuming and dropping it is
// the right answer, because a reveal is about the moment it was asked for.
//
// Call it after anything that can request one, and before draw().
void update(editor::State& state);

// Draws the category hierarchy into the current window, revealing whatever update() worked
// out. Categories are the tree; the XML files a pack is split across are an authoring
// convenience and do not appear as nodes. See docs/architecture.md for why.
void draw(editor::State& state);

// The node's full dotted category path, e.g. "haskha.arah.shogg" — a category's `id`, or a
// pathable's `type`. This is how a node is named away from the tree, where the surrounding
// hierarchy is not on screen to give a short name its meaning. Lives here because the tree
// is what decides how a node is named; anything else that has to name one calls this
// rather than growing its own.
std::string nodePath(const pack::Pack& pack, editor::NodeRef node);

} // namespace ui::tree_window
