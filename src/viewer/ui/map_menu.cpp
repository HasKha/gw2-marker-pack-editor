#include "viewer/ui/map_menu.h"

#include <imgui.h>

namespace viewer::ui::map_menu {

namespace {

constexpr const char* kMenuId = "##map_marker_menu";

// The category the menu was opened on, kept while it is open: the marker under the mouse may
// be gone by the time an item is chosen.
size_t menu_node = pack::kNoIndex;

// The tooltip's size the last frame it was drawn: where it goes is decided before it is laid
// out. A tip that changes size is placed for the old one for a single frame.
glm::vec2 tooltip_size{0.0f};

} // namespace

bool draw(const Context& context) {
    const ImGuiIO& io = ImGui::GetIO();
    // Over one of our own windows the mouse is theirs.
    if (!io.WantCaptureMouse) {
        const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
        const map_scene::Icon* icon = map_scene::hovered(*context.scene, *context.view, mouse);
        const runtime::LiveMap* live = icon && icon->layer < context.live.size() ? context.live[icon->layer] : nullptr;
        if (live && icon->marker < live->markers.size()) {
            const runtime::LiveMarker& marker = live->markers[icon->marker];
            if (context.tooltips) {
                const map_scene::Tooltip tip = map_scene::tooltip(marker, *context.tree, context.player, io.KeyShift);
                if (!tip.title.empty()) {
                    const glm::vec2 display(io.DisplaySize.x, io.DisplaySize.y);
                    const glm::vec2 position = map_scene::tooltipPosition(mouse, tooltip_size, display);
                    ImGui::SetNextWindowPos(ImVec2(position.x, position.y));
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(tip.title.c_str());
                    if (!tip.description.empty()) {
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                        ImGui::TextDisabled("%s", tip.description.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    tooltip_size = glm::vec2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
                    ImGui::EndTooltip();
                }
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && marker.node < context.tree->nodes.size()) {
                menu_node = marker.node;
                ImGui::OpenPopup(kMenuId);
            }
        }
    }

    bool hid = false;
    if (ImGui::BeginPopup(kMenuId)) {
        if (menu_node < context.tree->nodes.size()) {
            const registry::Category& category = context.tree->nodes[menu_node];
            ImGui::TextDisabled("%s", category.label.c_str());
            if (ImGui::MenuItem("Hide parent category")) {
                context.states->set(category.id, false, category.default_toggle);
                hid = true;
            }
            // Pathing offers the namespace only with Shift held; it is the id packs write.
            if (io.KeyShift && ImGui::MenuItem("Copy parent category namespace") && context.copy) context.copy(category.id);
        }
        ImGui::EndPopup();
    }
    return hid;
}

} // namespace viewer::ui::map_menu
