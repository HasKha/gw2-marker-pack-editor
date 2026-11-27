#include "viewer/ui/map_menu.h"

#include <imgui.h>

#include <algorithm>
#include <cstdlib>

#include "viewer/ui/style.h"

namespace viewer::ui::map_menu {

namespace {

constexpr const char* kMenuId = "##map_marker_menu";

// The categories from the root down to `node`, which must be in the tree.
std::vector<size_t> chain(const registry::CategoryTree& tree, size_t node) {
    std::vector<size_t> nodes;
    for (; node != pack::kNoIndex; node = tree.nodes[node].parent) nodes.push_back(node);
    std::reverse(nodes.begin(), nodes.end());
    return nodes;
}

void drawTooltip(const runtime::LiveMarker& marker, const Context& context, glm::vec2 mouse, View* view) {
    const map_scene::Tooltip tip = map_scene::tooltip(marker, context.player);
    if (tip.title.empty()) return;
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 display(io.DisplaySize.x, io.DisplaySize.y);
    const glm::vec2 position = map_scene::tooltipPosition(mouse, view->tooltip_size, display);
    ImGui::SetNextWindowPos(ImVec2(position.x, position.y));
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(tip.title.c_str());
    ImGui::TextDisabled("%s", tip.distance.c_str());
    view->tooltip_size = glm::vec2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
    ImGui::EndTooltip();
}

// One box per category from the root down to the marker's, each a step further in. A category
// under one turned off is drawn faint: its markers are hidden whatever its own box says.
bool drawCategories(const Context& context, size_t node) {
    bool changed = false;
    bool parent_off = false;
    const float step = ImGui::GetFrameHeight() * 0.5f;
    const std::vector<size_t> nodes = chain(*context.tree, node);
    for (size_t depth = 0; depth < nodes.size(); ++depth) {
        const registry::Category& category = context.tree->nodes[nodes[depth]];
        bool on = context.states->enabled(category.id, category.default_toggle);
        ImGui::PushID(static_cast<int>(nodes[depth]));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + step * static_cast<float>(depth));
        if (parent_off) ImGui::PushStyleColor(ImGuiCol_Text, style::muted());
        if (ImGui::Checkbox(category.label.c_str(), &on)) {
            context.states->set(category.id, on, category.default_toggle);
            changed = true;
        }
        if (parent_off) ImGui::PopStyleColor();
        if (parent_off && ImGui::IsItemHovered()) ImGui::SetTooltip("Hidden by a parent category");
        ImGui::PopID();
        parent_off |= !on;
    }
    return changed;
}

} // namespace

void ClickTracker::down(glm::ivec2 at) {
    armed = true;
    origin = at;
}

void ClickTracker::moved(glm::ivec2 at, bool cursor_visible) {
    if (!cursor_visible || std::abs(at.x - origin.x) > kDragSlack || std::abs(at.y - origin.y) > kDragSlack) armed = false;
}

bool ClickTracker::up(bool cursor_visible) {
    const bool click = armed && cursor_visible;
    armed = false;
    return click;
}

bool draw(const Context& context, View* view) {
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
    // The marker under the mouse. Over one of our own windows the mouse is theirs.
    const bool over_map = !io.WantCaptureMouse;
    const runtime::LiveMarker* marker = nullptr;
    if (over_map) {
        const map_scene::Icon* icon = map_scene::hovered(*context.scene, *context.view, mouse);
        const runtime::LiveMap* live = icon && icon->layer < context.live.size() ? context.live[icon->layer] : nullptr;
        if (live && icon->marker < live->markers.size()) marker = &live->markers[icon->marker];
    }

    if (marker && context.tooltips) drawTooltip(*marker, context, mouse, view);

    // On the release, which ImGui's own context menus wait for too: a frame in which ImGui sees a
    // right press closes every popup not under the mouse once it ends, which one opened on the
    // press is not yet. With this menu open it does see one, as an open popup wants the mouse.
    if (context.right_clicked && over_map) {
        // The marker's category, else the trail's under it: icons are drawn over every trail.
        size_t node = marker ? marker->node : pack::kNoIndex;
        if (!marker) {
            const map_scene::Path* path = map_scene::hoveredTrail(*context.scene, *context.view, mouse);
            const runtime::LiveMap* live = path && path->layer < context.live.size() ? context.live[path->layer] : nullptr;
            if (live && path->trail < live->trails.size()) node = live->trails[path->trail].node;
        }
        if (node < context.tree->nodes.size()) {
            view->menu_node = node;
            ImGui::OpenPopup(kMenuId);
        }
    }

    bool changed = false;
    if (ImGui::BeginPopup(kMenuId)) {
        if (view->menu_node < context.tree->nodes.size()) {
            changed = drawCategories(context, view->menu_node);
            // Pathing offers the namespace only with Shift held; it is the id packs write.
            if (io.KeyShift) {
                ImGui::Separator();
                const std::string& id = context.tree->nodes[view->menu_node].id;
                if (ImGui::MenuItem("Copy category namespace") && context.copy) context.copy(id);
            }
        }
        ImGui::EndPopup();
    }
    return changed;
}

} // namespace viewer::ui::map_menu
