#include "viewer/ui/main_menu.h"

#include <imgui.h>

#include <string>
#include <vector>

#include "viewer/runtime.h"

namespace viewer::ui::main_menu {

namespace {

// One row: the box when there is one, then the name, as a menu that opens on hover when it has
// children and a selectable otherwise. True when the box, or a leaf's name, was clicked. With
// children and open, the caller draws them and ends the menu: `*open` says so.
bool row(const char* name, const bool* checked, bool has_children, bool* open) {
    bool clicked = false;
    *open = false;
    if (checked) {
        bool value = *checked;
        clicked = ImGui::Checkbox("##on", &value);
        ImGui::SameLine();
    }
    if (has_children) {
        *open = ImGui::BeginMenu(name);
    } else if (ImGui::Selectable(name, false, ImGuiSelectableFlags_DontClosePopups)) {
        clicked = true;
    }
    return clicked;
}

// One category, and its children in a menu of their own while it is hovered. Pathing leaves out
// what `ishidden` hides, and what is under it.
bool drawCategory(const registry::CategoryTree& tree, size_t index, store::CategoryStates* states) {
    const registry::Category& node = tree.nodes[index];
    if (node.is_hidden) return false;

    // A separator is a heading between its siblings, with nothing to turn on or off.
    if (node.is_separator) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", node.label.c_str());
        return false;
    }

    ImGui::PushID(static_cast<int>(index));
    const bool enabled = states->enabled(node.id, node.default_toggle);
    bool open = false;
    bool changed = false;
    if (row(node.label.c_str(), &enabled, !node.children.empty(), &open)) {
        states->set(node.id, !enabled, node.default_toggle);
        changed = true;
    }
    if (open) {
        for (size_t child : node.children) changed |= drawCategory(tree, child, states);
        ImGui::EndMenu();
    }
    ImGui::PopID();
    return changed;
}

void drawScriptEntry(script::Engine* engine, size_t index) {
    const script::MenuNode& node = engine->menus()[index];
    if (node.removed) return;

    ImGui::PushID(static_cast<int>(index));
    bool open = false;
    if (row(node.name.c_str(), node.can_check ? &node.checked : nullptr, !node.children.empty(), &open)) engine->clickMenu(index);
    if (!open && ImGui::IsItemHovered() && !engine->menus()[index].tooltip.empty()) {
        ImGui::SetTooltip("%s", engine->menus()[index].tooltip.c_str());
    }
    if (open) {
        // Copied: a click can add entries, which moves them all.
        const std::vector<size_t> children = engine->menus()[index].children;
        for (size_t child : children) drawScriptEntry(engine, child);
        ImGui::EndMenu();
    }
    ImGui::PopID();
}

} // namespace

bool drawItems(const Context& context) {
    bool changed = false;

    ImGui::PushID("all_markers");
    const bool all_on = context.states->enabled(runtime::ALL_MARKERS, true);
    bool open = false;
    if (row("All Markers", &all_on, true, &open)) {
        context.states->set(runtime::ALL_MARKERS, !all_on, true);
        changed = true;
    }
    if (open) {
        if (context.tree->roots.empty()) ImGui::TextDisabled("No packs loaded.");
        for (size_t root : context.tree->roots) changed |= drawCategory(*context.tree, root, context.states);
        ImGui::EndMenu();
    }
    ImGui::PopID();

    if (context.scripts) {
        const std::vector<script::MenuNode>& menus = context.scripts->menus();
        if (!menus.empty() && !menus.front().children.empty()) {
            ImGui::PushID("scripts");
            if (ImGui::BeginMenu("Scripts")) {
                const std::vector<size_t> children = menus.front().children;
                for (size_t child : children) drawScriptEntry(context.scripts, child);
                ImGui::EndMenu();
            }
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    if (ImGui::Selectable("Manage Marker Packs") && context.manage_packs) context.manage_packs();
    // Not there with no editor to start.
    if (context.edit_pack && ImGui::Selectable("Edit Marker Pack")) context.edit_pack();
    return changed;
}

} // namespace viewer::ui::main_menu
