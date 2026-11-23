#include "viewer/ui/main_menu.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>
// For ImGuiItemFlags_Disabled: imgui 1.80 has no BeginDisabled.
#include <imgui_internal.h>

#include <string>
#include <vector>

#include "viewer/runtime.h"
#include "viewer/ui/achievement_tooltip.h"
#include "viewer/ui/fonts.h"

namespace viewer::ui::main_menu {

namespace {

// One row: the box when there is one, then the name, as a menu that opens on hover when it has
// children and a selectable otherwise. True when the box or the name was clicked; never while
// `locked`, whose box is greyed out, though its children still open. With children and open,
// the caller draws them and ends the menu: `*open` says so. `*hovered` is the mouse over the
// row, read off its rectangle since an open menu's last item is in the menu beside it.
bool row(const char* name, const bool* checked, bool has_children, bool* open, bool* hovered, bool locked = false) {
    bool clicked = false;
    *open = false;
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const ImVec2 row_max(row_min.x + ImGui::GetContentRegionAvail().x, row_min.y + ImGui::GetFrameHeight());
    *hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(row_min, row_max);
    if (checked) {
        bool value = *checked;
        if (locked) {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        }
        clicked = ImGui::Checkbox("##on", &value);
        if (locked) {
            ImGui::PopStyleVar();
            ImGui::PopItemFlag();
        }
        ImGui::SameLine();
    }
    if (!has_children) {
        return (ImGui::Selectable(name, false, ImGuiSelectableFlags_DontClosePopups) || clicked) && !locked;
    }

    // A click on a menu's name is the menu's, and once it opens ImGui's last item is in the menu
    // beside it, so the name's click is read off its rectangle, as BeginMenu lays it out.
    const ImVec2 name_min = ImGui::GetCursorScreenPos();
    const ImVec2 name_max(name_min.x + ImGui::GetContentRegionAvail().x, name_min.y + ImGui::GetTextLineHeight());
    const bool name_clicked = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(name_min, name_max) && ImGui::IsMouseClicked(0);
    *open = ImGui::BeginMenu(name);
    return (clicked || (checked && name_clicked)) && !locked;
}

// Per child of a menu, whether it is drawn: smart categories leave out what is not on the map,
// and a separator stays only above something drawn. How many categories were left out.
size_t chooseShown(
    const registry::CategoryTree& tree,
    const std::vector<size_t>& children,
    const Context& context,
    size_t parent,
    std::vector<char>* shown) {
    shown->assign(children.size(), 1);
    const bool filtered = context.on_map && !(context.show_all && context.show_all->count(parent));
    size_t skipped = 0;
    bool below = false; // something drawn below, up to the next separator
    for (size_t i = children.size(); i-- > 0;) {
        const registry::Category& node = tree.nodes[children[i]];
        if (node.is_hidden) {
            (*shown)[i] = 0;
        } else if (node.is_separator) {
            (*shown)[i] = !filtered || below;
            below = false;
        } else if (filtered && !(children[i] < context.on_map->size() && (*context.on_map)[children[i]])) {
            (*shown)[i] = 0;
            ++skipped;
        } else {
            below = true;
        }
    }
    return skipped;
}

bool drawChildren(const registry::CategoryTree& tree, const std::vector<size_t>& children, size_t parent, const Context& context);

// One category, and its children in a menu of their own while it is hovered. Pathing leaves out
// what `ishidden` hides, and what is under it.
bool drawCategory(const registry::CategoryTree& tree, size_t index, const Context& context) {
    const registry::Category& node = tree.nodes[index];
    store::CategoryStates* states = context.states;

    // A separator is a heading between its siblings, with nothing to turn on or off.
    if (node.is_separator) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", node.label.c_str());
        return false;
    }

    ImGui::PushID(static_cast<int>(index));
    // PathingCategoryNode: shown off once its achievement is done, the player's own choice kept
    // underneath. Its markers hide by their own achievement filter, which they inherit.
    const bool done = context.auto_hide && context.account && node.achievement_id &&
                      context.account->achievementHidden(*node.achievement_id, node.achievement_bit);
    const bool enabled = !done && states->enabled(node.id, node.default_toggle);
    bool open = false;
    bool changed = false;
    bool hovered = false;
    if (row(node.label.c_str(), &enabled, !node.children.empty(), &open, &hovered, done)) {
        states->set(node.id, !enabled, node.default_toggle);
        changed = true;
    }
    if (hovered && node.achievement_id) {
        // CategoryContextMenuStripItem's tooltip, with the category's own bit.
        const achievements::Details* details =
            context.achievement_tooltips && context.achievements ? context.achievements->find(*node.achievement_id) : nullptr;
        if (details || done) {
            ImGui::BeginTooltip();
            if (details) achievement_tooltip::draw(*details, node.achievement_bit);
            if (details && done) ImGui::Separator();
            if (done) ImGui::Text("You've completed this achievement.");
            ImGui::EndTooltip();
        }
    }
    if (open) {
        changed |= drawChildren(tree, node.children, index, context);
        ImGui::EndMenu();
    }
    ImGui::PopID();
    return changed;
}

// A menu's categories, and Pathing's "N Categories Are Hidden" under them, which shows them.
bool drawChildren(const registry::CategoryTree& tree, const std::vector<size_t>& children, size_t parent, const Context& context) {
    std::vector<char> shown;
    const size_t skipped = chooseShown(tree, children, context, parent, &shown);
    bool changed = false;
    for (size_t i = 0; i < children.size(); ++i) {
        if (shown[i]) changed |= drawCategory(tree, children[i], context);
    }
    if (skipped > 0) {
        const std::string label = fonts::label(
            ICON_FA_EYE_SLASH,
            (std::to_string(skipped) + (skipped == 1 ? " Category Is Hidden" : " Categories Are Hidden")).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_DontClosePopups) && context.show_all) {
            context.show_all->insert(parent);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hidden because they are for markers on a different map. Click to show them.");
    }
    return changed;
}

void drawScriptEntry(script::Engine* engine, size_t index) {
    const script::MenuNode& node = engine->menus()[index];
    if (node.removed) return;

    ImGui::PushID(static_cast<int>(index));
    bool open = false;
    bool hovered = false;
    if (row(node.name.c_str(), node.can_check ? &node.checked : nullptr, !node.children.empty(), &open, &hovered)) engine->clickMenu(index);
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
    bool hovered = false;
    if (row("All Markers", &all_on, true, &open, &hovered)) {
        context.states->set(runtime::ALL_MARKERS, !all_on, true);
        changed = true;
    }
    if (open) {
        if (context.tree->roots.empty()) ImGui::TextDisabled("No packs loaded.");
        changed |= drawChildren(*context.tree, context.tree->roots, pack::kNoIndex, context);
        ImGui::EndMenu();
    }
    ImGui::PopID();

    if (context.scripts) {
        const std::vector<script::MenuNode>& menus = context.scripts->menus();
        if (!menus.empty() && !menus.front().children.empty()) {
            ImGui::PushID("scripts");
            if (ImGui::BeginMenu((fonts::label(ICON_FA_SCROLL, "Scripts") + "###scripts").c_str())) {
                const std::vector<size_t> children = menus.front().children;
                for (size_t child : children) drawScriptEntry(context.scripts, child);
                ImGui::EndMenu();
            }
            ImGui::PopID();
        }
    }

    if (context.separator) ImGui::Separator();
    if (ImGui::Selectable(fonts::label(ICON_FA_BOX_ARCHIVE, "Manage Marker Packs").c_str()) && context.manage_packs) context.manage_packs();
    if (ImGui::Selectable(fonts::label(ICON_FA_GEAR, "Options").c_str()) && context.open_options) context.open_options();
    if (context.open_editor && ImGui::Selectable(fonts::label(ICON_FA_PEN_TO_SQUARE, "Open Editor...").c_str())) context.open_editor();
    return changed;
}

} // namespace viewer::ui::main_menu
