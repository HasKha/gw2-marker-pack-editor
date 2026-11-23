#pragma once

#include <functional>

#include "viewer/registry.h"
#include "viewer/script/engine.h"
#include "viewer/store.h"

// The addon's one menu, Blish HUD's: All Markers and Scripts as Pathing draws them, then Manage
// Marker Packs and Edit Marker Pack. The same items wherever it is shown — Nexus's own menu, and
// the menu of our icon in Nexus's bar, left click or right.
namespace viewer::ui::main_menu {

struct Context {
    const registry::CategoryTree* tree = nullptr;
    store::CategoryStates* states = nullptr;
    script::Engine* scripts = nullptr; // null while scripts do not run
    std::function<void()> manage_packs; // opens Marker Packs
    std::function<void()> edit_pack; // starts the editor; null leaves the item out
};

// Draws the items into a popup or menu already begun. True when a category was turned on or off.
//
// Every item is a row "[ ] name >": the box turns it on or off, and hovering one with children
// opens them in a menu of their own, beside it where it fits.
bool drawItems(const Context& context);

} // namespace viewer::ui::main_menu
