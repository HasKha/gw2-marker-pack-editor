#pragma once

#include <glm/vec3.hpp>

#include "editor_state.h"

// Creating and deleting markers. The pack layer does the document surgery; this decides
// what a new marker should be and where it belongs, which is an editor question.
namespace pathable_edit {

// Adds a POI at `position` on map `map_id`, placed by what is selected:
//
//   category selected -> the last marker in that category
//   marker selected   -> immediately after it, as a sibling
//
// A marker's attributes are copied onto the new one — everything that applies to a POI —
// so it inherits its icon, size and behaviour, and only its position, map and GUID are its
// own. The new marker is selected and revealed in the tree.
//
// Returns its index in Pack::pathables, or pack::kNoIndex when nothing is selected to add
// next to.
size_t addPoi(editor::State& state, const glm::vec3& position, int32_t map_id);

// Adds a trail under the same rules, pointing at a new empty `.trl` written into the pack
// beside whatever trail data it already ships. `map_id` goes into the file's header, which is
// what decides a trail's map, and into the `MapID` attribute, which is documentation. A trail
// with no points draws nothing yet, so its points come next in the trail editor.
size_t addTrail(editor::State& state, int32_t map_id);

// Gives `attributes` an explicit `MapID` of `map_id` unless the value it already resolves to
// — set on the node, or inherited from `category` and its ancestors — says the same thing.
//
// Why the exception: a pack that declares `MapID` once on a category should not collect a
// redundant copy on every marker the editor adds beneath it. Why it is needed at all: a
// marker with no map id resolves to none, and both Blish and our own renderer file a marker
// by map before anything else, so one with no map is one that never draws. (A *trail* takes
// its map from its `.trl` header in both, so there the attribute is documentation only.)
//
// Returns true when it wrote something. Shared with the trail editor, which has to hold a
// trail's `.trl` header and its attribute to the same value.
bool setMapId(const editor::State& state, pack::AttributeCollection* attributes, size_t category, int32_t map_id);

// Side effects of an attribute edit beyond the value itself. `previous` is what the
// attribute held before the edit; call it once the value has settled, not per keystroke,
// and inside the caller's undo::Step.
//
// One attribute needs this: `traildata` is a trail's only name — the tree labels it with
// that path and the trail editor shows it — so renaming it is how a trail gets named, and
// the `.trl` has to move too or the points are left behind under the old path. A new path
// that already names data is a re-point, not a rename: nothing is written over. The old
// file is dropped only when the editor added it and no other trail resolves to it.
//
// A no-op for everything else.
void attributeRenamed(editor::State& state, editor::NodeRef node, const std::string& key, const std::string& previous);

// Adds a category at the dotted id `name` — e.g. "root.foo.bar" — nested under whatever
// already exists at "root.foo". No dot at all makes a root category. This is also how
// placement is chosen: editing just the last segment of a selected category's own id makes
// a sibling, appending a segment makes a child, and trimming segments walks back up its
// ancestors — so there is no separate placement to choose.
//
// `display_name` becomes the category's DisplayName and is required; `description`, when
// non-empty, becomes its tip-description. With `copy_attributes` set, every attribute the
// selected category (or a selected marker's filing category) carries — its DisplayName and
// tip-description included — is copied onto the new one first, so `display_name` and a
// non-empty `description` still win.
//
// Returns the new category's index, or pack::kNoIndex when `name` or `display_name` is
// empty, `name`'s parent segments do not name an existing category, its last segment is
// empty or contains no valid characters, or the resulting id is already taken.
size_t addCategory(
    editor::State& state,
    const std::string& name,
    const std::string& display_name,
    const std::string& description,
    bool copy_attributes);

// What deleting `node` would take with it. A category takes its whole subtree: the
// categories nested under it and every marker in any of them.
struct Removal {
    size_t categories = 0;
    size_t pathables = 0;

    bool empty() const { return categories == 0 && pathables == 0; }
};

Removal wouldRemove(const editor::State& state, editor::NodeRef node);

// Deletes `node`, and drops it from the selection: the thing it pointed at is gone. What
// else was selected stays. Nothing is erased from the pack's vectors — see the tombstone note in docs/architecture.md — so
// every other index survives.
Removal remove(editor::State& state, editor::NodeRef node);

} // namespace pathable_edit
