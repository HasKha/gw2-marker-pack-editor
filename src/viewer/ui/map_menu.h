#pragma once

#include <glm/vec3.hpp>

#include <functional>
#include <string>
#include <vector>

#include "viewer/live/runtime.h"
#include "viewer/maps/map.h"
#include "viewer/maps/map_scene.h"
#include "viewer/packs/registry.h"
#include "viewer/store.h"

// What the mouse does over a marker on the minimap or the full map: FlatMap's tooltip, and its
// menu on a right click. The click still reaches the game, as FlatMap does not block it.
namespace viewer::ui::map_menu {

struct Context {
    const map_scene::MapScene* scene = nullptr;
    const map::View* view = nullptr;
    std::vector<const runtime::LiveMap*> live; // what the scene's icons index, by their layer
    const registry::CategoryTree* tree = nullptr;
    store::CategoryStates* states = nullptr;
    glm::vec3 player{0.0f}; // Blish HUD's axes, for how far away
    bool tooltips = true; // "Show Tooltips on Map"
    std::function<void(const std::string& text)> copy; // to the clipboard
};

// Draws the tooltip of the marker under the mouse and opens the menu on a right click; then
// the menu, while it is open. True when the menu turned a category off.
bool draw(const Context& context);

} // namespace viewer::ui::map_menu
