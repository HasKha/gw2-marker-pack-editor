#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <functional>
#include <string>
#include <vector>

#include "viewer/live/runtime.h"
#include "viewer/maps/map.h"
#include "viewer/maps/map_scene.h"
#include "viewer/packs/registry.h"
#include "viewer/store.h"

// What the mouse does over the minimap or the full map: over a marker, a tooltip with its name
// and distance; and on a right click on a marker or a trail, a menu of the categories it sits in,
// each with its box. The click still reaches the game, as FlatMap does not block it.
namespace viewer::ui::map_menu {

// Tells a right click from a right drag, which turns the camera, as the window procedure sees the
// button go down and up. Nexus hands a click to ImGui only once ImGui already wants the mouse,
// which over the game's map it does not, so ImGui never sees this one. Down and up positions
// cannot tell them apart: Nexus's `MouseResetFix` puts the cursor back where it went down. The
// game hiding the cursor can, and moving past a few pixels for a drag that keeps it shown.
class ClickTracker
{
public:
    void down(glm::ivec2 at);
    void moved(glm::ivec2 at, bool cursor_visible);
    // True when it was a click.
    bool up(bool cursor_visible);

private:
    static constexpr int kDragSlack = 4; // px

    bool armed_ = false;
    glm::ivec2 origin_{0};
};

struct Context {
    const map_scene::MapScene* scene = nullptr;
    const map::View* view = nullptr;
    std::vector<const runtime::LiveMap*> live; // what the scene's icons index, by their layer
    const registry::CategoryTree* tree = nullptr;
    store::CategoryStates* states = nullptr;
    glm::vec3 player{0.0f}; // Blish HUD's axes, for how far away
    bool tooltips = true; // "Show Tooltips on Map"
    bool right_clicked = false; // since the last frame, by a ClickTracker
    std::function<void(const std::string& text)> copy; // to the clipboard
};

// What the menu keeps between frames.
struct View {
    // The category the menu was opened on, kept while it is open: the marker under the mouse may
    // be gone by the time an item is chosen.
    size_t menu_node = pack::kNoIndex;
    // The tooltip's size the last frame it was drawn: where it goes is decided before it is laid
    // out. A tip that changes size is placed for the old one for a single frame.
    glm::vec2 tooltip_size{0.0f};
};

// Draws the tooltip of the marker under the mouse, and opens the menu on `right_clicked` over a
// marker or a trail; then the menu, while it is open. True when the menu turned a category on
// or off.
bool draw(const Context& context, View* view);

} // namespace viewer::ui::map_menu
