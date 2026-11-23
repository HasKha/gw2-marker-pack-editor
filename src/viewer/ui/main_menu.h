#pragma once

#include <functional>
#include <set>
#include <vector>

#include "viewer/registry.h"
#include "viewer/script/engine.h"
#include "viewer/store.h"

// The addon's one menu, Blish HUD's: All Markers and Scripts as Pathing draws them, then Manage
// Marker Packs, Options and Open Editor. The same items wherever it is shown — Nexus's own menu, and the
// menu of our icon in Nexus's bar, left click or right.
namespace viewer::ui::main_menu {

struct Context {
    const registry::CategoryTree* tree = nullptr;
    store::CategoryStates* states = nullptr;
    script::Engine* scripts = nullptr; // null while scripts do not run
    std::function<void()> manage_packs; // opens Marker Packs
    std::function<void()> open_options; // opens the options page in a window of ours
    std::function<void()> open_editor; // null leaves the item out

    // Pathing's smart categories: with `on_map` (runtime::nodesOnMap), a category with nothing on
    // this map is left out, behind a row that shows them. Null shows everything.
    const std::vector<char>* on_map = nullptr;
    // The categories whose children are all shown anyway, kNoIndex for All Markers' own: what the
    // row was clicked in. The caller forgets them when the map changes.
    std::set<size_t>* show_all = nullptr;

    // The separator above Manage Marker Packs. Left out of Nexus's own menu, which already draws
    // one between each addon's items and the next.
    bool separator = true;
};

// Draws the items into a popup or menu already begun. True when a category was turned on or off.
//
// Every item is a row "[ ] name >": the box or the name turns it on or off, and hovering one with
// children opens them in a menu of their own, beside it where it fits.
bool drawItems(const Context& context);

} // namespace viewer::ui::main_menu
