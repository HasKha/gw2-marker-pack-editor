#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "editor/editor_state.h"

namespace ui::tree_window {

// A node picked in the world has to be shown in the tree, which means opening every category
// above it. A parent chain is only walkable upwards, so it is collected once by update() and
// consulted on the way down, for the one frame that takes State::reveal_pending.
struct Reveal {
    bool active = false;
    editor::NodeRef target;
    std::vector<size_t> ancestors; // categories to force open, the target's own first
    bool orphans = false; // the target is a pathable with no resolvable category

    bool opens(size_t category) const;
};

// Takes the pending reveal off State and works out which categories to open for it. Runs every
// frame, drawn or not, so a reveal is dropped rather than replayed when the tree comes back. Call
// after anything that can request one, and hand what it returns to draw().
Reveal update(editor::State& state);

// Draws the category hierarchy into the current window, revealing what update() worked out.
void draw(editor::State& state, const Reveal& reveal);

// The node's full dotted category path, e.g. "haskha.arah.shogg": how a node is named away from
// the tree.
std::string nodePath(const pack::Pack& pack, editor::NodeRef node);

} // namespace ui::tree_window
