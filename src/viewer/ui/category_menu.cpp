#include "viewer/ui/category_menu.h"

#include <imgui.h>

namespace viewer::ui::category_menu {

namespace {

// One category and, while it is open, everything under it. Tree nodes start closed, so a
// pack with tens of thousands of categories costs only what is open.
bool drawNode(const registry::CategoryTree& tree, size_t index, store::CategoryStates* states) {
    const registry::Category& node = tree.nodes[index];

    // ishidden keeps a category out of the menu, and what is under it with it: Pathing's
    // menu does not list it, and nothing under it is reachable without it.
    if (node.is_hidden) return false;

    // A separator is a heading between its siblings, with nothing to toggle.
    if (node.is_separator) {
        ImGui::Separator();
        ImGui::Text("%s", node.label.c_str());
        return false;
    }

    bool changed = false;
    ImGui::PushID(static_cast<int>(index));

    bool enabled = states->enabled(node.id, node.default_toggle);
    if (ImGui::Checkbox("##enabled", &enabled)) {
        states->set(node.id, enabled, node.default_toggle);
        changed = true;
    }
    ImGui::SameLine();

    if (node.children.empty()) {
        ImGui::Text("%s", node.label.c_str());
    } else if (ImGui::TreeNodeEx("##children", ImGuiTreeNodeFlags_None, "%s", node.label.c_str())) {
        for (size_t child : node.children) changed |= drawNode(tree, child, states);
        ImGui::TreePop();
    }

    ImGui::PopID();
    return changed;
}

} // namespace

bool draw(const registry::CategoryTree& tree, store::CategoryStates* states, bool* open, const std::string& status) {
    if (!*open) return false;

    ImGui::SetNextWindowSize(ImVec2(360.0f, 480.0f), ImGuiCond_FirstUseEver);
    bool changed = false;
    if (ImGui::Begin("Markers", open)) {
        if (!status.empty()) ImGui::Text("%s", status.c_str());
        if (tree.roots.empty()) {
            ImGui::TextDisabled("No packs loaded.");
        } else {
            for (size_t root : tree.roots) changed |= drawNode(tree, root, states);
        }
    }
    ImGui::End();
    return changed;
}

} // namespace viewer::ui::category_menu
