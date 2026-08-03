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

} // namespace pathable_edit
