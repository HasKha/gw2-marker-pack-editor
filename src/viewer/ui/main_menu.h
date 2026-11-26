#pragma once

#include <functional>
#include <set>
#include <vector>

#include "viewer/gw2_api/account.h"
#include "viewer/gw2_api/achievements.h"
#include "viewer/packs/registry.h"
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

    // With `auto_hide`, Pathing's "Allow Markers to Automatically Hide", a category whose
    // achievement `account` lists as done is shown off and cannot be turned on or off. Null
    // knows of nothing done.
    const account::Progress* account = nullptr;
    bool auto_hide = true;
    // With `achievement_tooltips`, Pathing's "Show Tooltips for Achievements", hovering a
    // category tied to an achievement shows its details from here. Null shows none.
    achievements::Catalog* achievements = nullptr;
    bool achievement_tooltips = true;

    // The separator above Manage Packs. Left out of Nexus's own menu, which already draws
    // one between each addon's items and the next.
    bool separator = true;
};

// Places the next window, the menu's popup, under our icon in Nexus's bar, or above it in the
// lower half of the screen, and likewise left or right of it. Under the mouse when the icon
// cannot be found.
void placeUnderIcon();

// Draws the items into a popup or menu already begun. True when a category was turned on or off.
//
// Every item is a row "[ ] name >": the box or the name turns it on or off, and hovering one with
// children opens them in a menu of their own, beside it where it fits.
bool drawItems(const Context& context);

} // namespace viewer::ui::main_menu
