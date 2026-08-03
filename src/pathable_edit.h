#pragma once

#include <glm/vec3.hpp>

#include "editor_state.h"

// Creating and deleting markers. The pack layer does the document surgery; this decides
// what a new marker should be and where it belongs, which is an editor question.
namespace pathable_edit {

// Adds a POI at `position`, placed by what is selected:
//
//   category selected -> the last marker in that category
//   marker selected   -> immediately after it, as a sibling
//
// A marker's attributes are copied onto the new one — everything that applies to a POI —
// so it inherits its icon, size and behaviour, and only its position and GUID are its own.
// The new marker is selected and revealed in the tree.
//
// Returns its index in Pack::pathables, or pack::kNoIndex when nothing is selected to add
// next to.
size_t addPoi(editor::State& state, const glm::vec3& position);

// What deleting `node` would take with it. A category takes its whole subtree: the
// categories nested under it and every marker in any of them.
struct Removal {
    size_t categories = 0;
    size_t pathables = 0;

    bool empty() const { return categories == 0 && pathables == 0; }
};

Removal wouldRemove(const editor::State& state, editor::NodeRef node);

// Deletes `node`, and clears the selection: the thing it pointed at is gone. Nothing is
// erased from the pack's vectors — see the tombstone note in docs/architecture.md — so
// every other index survives.
Removal remove(editor::State& state, editor::NodeRef node);

} // namespace pathable_edit
