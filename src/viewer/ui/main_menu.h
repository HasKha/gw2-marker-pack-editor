#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "viewer/gw2_api/account.h"
#include "viewer/gw2_api/achievements.h"
#include "viewer/packs/registry.h"
#include "viewer/script/engine.h"
#include "viewer/store.h"
#include "viewer/ui/style.h"

// The addon's one menu, Blish HUD's: All Markers and Scripts as Pathing draws them, each menu of
// categories with our Select All at its foot, then Manage Marker Packs, Options and Open Editor.
// The same items wherever it is shown — Nexus's own menu, and the menu of our icon in Nexus's
// bar, left click or right.
namespace viewer::ui::main_menu {

// What a click on a menu's "N Categories Are Hidden" leaves, as Pathing's
// CategoryContextMenuStrip has it: that menu shows every category, and so does every menu under
// it, until it closes. By menuId() rather than by node, since a pack removed or read again builds
// the tree anew and moves the nodes.
struct ShowAll {
    std::set<std::string> menus; // clicked, and open since
    std::set<std::string> open; // the menus drawn this frame, so far
    int frame = -1; // the last frame the menu was drawn on, imgui's count

    // Before the menu is drawn on `frame`: one not drawn on the frame before was closed, and
    // every menu in it with it.
    void begin(int frame);
    // Every menu drawn on its frame, as drawn.
    void drawn(const std::string& menu) { open.insert(menu); }
    // After the menu is drawn: a menu clicked but not drawn this frame was closed.
    void end();
};

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
    // The menus whose hidden categories were asked for, kept across frames by the caller. Null
    // leaves the row unclickable.
    ShowAll* show_all = nullptr;

    // With `auto_hide`, Pathing's "Allow Markers to Automatically Hide", a category whose
    // achievement `account` lists as done is shown off and cannot be turned on or off. Null
    // knows of nothing done.
    const account::Progress* account = nullptr;
    bool auto_hide = true;
    // With `achievement_tooltips`, Pathing's "Show Tooltips for Achievements", hovering a
    // category tied to an achievement shows its details from here. Null shows none.
    achievements::Catalog* achievements = nullptr;
    bool achievement_tooltips = true;

    // Ours: each menu of categories ends with Select All, under a separator, a box that shows
    // whether all, some or none of the menu's categories are on, those smart categories hide
    // included, and turns them all on or off (selectAllTargets).
    bool select_all = true;

    // The separator above Manage Packs. Left out of Nexus's own menu, which already draws
    // one between each addon's items and the next.
    bool separator = true;
};

// What ShowAll names the menu of `parent`'s children by: its category's id, and "" for All
// Markers' own, kNoIndex, which no category has.
std::string menuId(const registry::CategoryTree& tree, size_t parent);

// Whether the menu of `parent`'s children was itself asked to show all of them, smart categories
// or not: its hidden categories' row was clicked. The menus under it show all of theirs too,
// as drawn, without being asked.
bool showsAll(const registry::CategoryTree& tree, size_t parent, const Context& context);

// Of `children`, the categories of one menu, those Select All turns on or off: every one, smart
// categories' hidden ones too, but for separators, what `ishidden` keeps out of the menu, and
// what a done achievement locks.
std::vector<size_t> selectAllTargets(const registry::CategoryTree& tree, const std::vector<size_t>& children, const Context& context);

// Whether none, some or all of `targets` are on.
style::TriState enabledOf(const registry::CategoryTree& tree, const std::vector<size_t>& targets, const Context& context);

// Turns every one of `targets` on, or off.
void selectAll(const registry::CategoryTree& tree, const std::vector<size_t>& targets, bool on, const Context& context);

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
