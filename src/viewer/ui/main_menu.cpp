#include "viewer/ui/main_menu.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>
// For the rectangle of the button ImGui last saw clicked: where our icon in Nexus's bar is.
#include <imgui_internal.h>

#include <string>
#include <vector>

#include "viewer/live/runtime.h"
#include "viewer/ui/achievement_tooltip.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/style.h"

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
        style::beginDisabled(locked);
        clicked = ImGui::Checkbox("##on", &value);
        style::endDisabled();
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
// unless the menu shows `everything`, and a separator stays only above something drawn. How
// many categories were left out.
size_t chooseShown(
    const registry::CategoryTree& tree,
    const std::vector<size_t>& children,
    const Context& context,
    bool everything,
    std::vector<char>* shown) {
    shown->assign(children.size(), 1);
    const bool filtered = context.on_map && !everything;
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

// PathingCategoryNode: shown off once its achievement is done, the player's own choice kept
// underneath, and not to be turned on or off. Its markers hide by their own achievement filter,
// which they inherit.
bool locked(const registry::Category& node, const Context& context) {
    return context.auto_hide && context.account && node.achievement_id &&
           context.account->achievementHidden(*node.achievement_id, node.achievement_bit);
}

// Select All, under a separator, a row as a category's is: its box or its name turns every one
// of `targets` on, or off when all of them are. Nothing when there is nothing to turn.
bool drawSelectAll(const registry::CategoryTree& tree, const std::vector<size_t>& targets, const Context& context) {
    if (targets.empty()) return false;
    ImGui::Separator();
    ImGui::PushID("select_all");
    const style::TriState state = enabledOf(tree, targets, context);
    const bool box = style::triStateCheckbox("##on", state);
    ImGui::SameLine();
    const bool name = ImGui::Selectable("Select All", false, ImGuiSelectableFlags_DontClosePopups);
    ImGui::PopID();
    if (!box && !name) return false;
    selectAll(tree, targets, state != style::TriState::On, context);
    return true;
}

bool drawChildren(
    const registry::CategoryTree& tree,
    const std::vector<size_t>& children,
    size_t parent,
    bool inherited,
    const Context& context);

// One category, and its children in a menu of their own while it is hovered, showing every one of
// them when `everything`. Pathing leaves out what `ishidden` hides, and what is under it.
bool drawCategory(const registry::CategoryTree& tree, size_t index, bool everything, const Context& context) {
    const registry::Category& node = tree.nodes[index];
    store::CategoryStates* states = context.states;

    // A separator is a heading between its siblings, with nothing to turn on or off.
    if (node.is_separator) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", node.label.c_str());
        return false;
    }

    ImGui::PushID(static_cast<int>(index));
    const bool done = locked(node, context);
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
        changed |= drawChildren(tree, node.children, index, everything, context);
        ImGui::EndMenu();
    }
    ImGui::PopID();
    return changed;
}

// A menu's categories, Pathing's "N Categories Are Hidden" under them, which shows them, and
// our Select All at the foot.
// `inherited` when a menu above it shows all of its own, which shows all of this one's too, as
// Pathing hands its forceShowAll down to every submenu it makes.
bool drawChildren(
    const registry::CategoryTree& tree,
    const std::vector<size_t>& children,
    size_t parent,
    bool inherited,
    const Context& context) {
    const std::string menu = menuId(tree, parent);
    if (context.show_all) context.show_all->drawn(menu);
    const bool everything = inherited || showsAll(tree, parent, context);
    std::vector<char> shown;
    const size_t skipped = chooseShown(tree, children, context, everything, &shown);
    bool changed = false;
    for (size_t i = 0; i < children.size(); ++i) {
        if (shown[i]) changed |= drawCategory(tree, children[i], everything, context);
    }
    if (skipped > 0) {
        const std::string label = fonts::label(
            ICON_FA_EYE_SLASH,
            (std::to_string(skipped) + (skipped == 1 ? " Category Is Hidden" : " Categories Are Hidden")).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, style::muted());
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_DontClosePopups) && context.show_all) {
            context.show_all->menus.insert(menu);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hidden because they have nothing on this map. Click to show them.");
    }
    if (context.select_all) changed |= drawSelectAll(tree, selectAllTargets(tree, children, context), context);
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

std::string menuId(const registry::CategoryTree& tree, size_t parent) {
    return parent == pack::kNoIndex ? std::string() : tree.nodes[parent].id;
}

bool showsAll(const registry::CategoryTree& tree, size_t parent, const Context& context) {
    return context.show_all && context.show_all->menus.count(menuId(tree, parent));
}

void ShowAll::begin(int now) {
    if (frame < now - 1) menus.clear();
    frame = now;
    open.clear();
}

void ShowAll::end() {
    std::erase_if(menus, [this](const std::string& menu) { return !open.count(menu); });
}

std::vector<size_t> selectAllTargets(const registry::CategoryTree& tree, const std::vector<size_t>& children, const Context& context) {
    std::vector<size_t> targets;
    for (size_t index : children) {
        const registry::Category& node = tree.nodes[index];
        if (!node.is_separator && !node.is_hidden && !locked(node, context)) targets.push_back(index);
    }
    return targets;
}

style::TriState enabledOf(const registry::CategoryTree& tree, const std::vector<size_t>& targets, const Context& context) {
    size_t on = 0;
    for (size_t index : targets) {
        const registry::Category& node = tree.nodes[index];
        on += context.states->enabled(node.id, node.default_toggle) ? 1 : 0;
    }
    if (on == 0) return style::TriState::Off;
    return on == targets.size() ? style::TriState::On : style::TriState::Mixed;
}

void selectAll(const registry::CategoryTree& tree, const std::vector<size_t>& targets, bool on, const Context& context) {
    for (size_t index : targets) {
        const registry::Category& node = tree.nodes[index];
        context.states->set(node.id, on, node.default_toggle);
    }
}

// Nexus's bar is an ImGui window, and ImGui keeps the rectangle of the button last clicked as its
// nav focus (SetFocusID in imgui.cpp): our icon, while it holds the mouse.
void placeUnderIcon() {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    ImRect icon(mouse, mouse);
    if (const ImGuiWindow* window = g.NavWindow) {
        const ImRect& relative = window->NavRectRel[0];
        const ImRect clicked(
            window->Pos.x + relative.Min.x,
            window->Pos.y + relative.Min.y,
            window->Pos.x + relative.Max.x,
            window->Pos.y + relative.Max.y);
        if (clicked.Contains(mouse)) icon = clicked;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool below = icon.GetCenter().y < display.y * 0.5f;
    const bool rightward = icon.GetCenter().x < display.x * 0.5f;
    ImGui::SetNextWindowPos(
        ImVec2(rightward ? icon.Min.x : icon.Max.x, below ? icon.Max.y : icon.Min.y),
        ImGuiCond_Always,
        ImVec2(rightward ? 0.0f : 1.0f, below ? 0.0f : 1.0f));
}

bool drawItems(const Context& context) {
    bool changed = false;
    if (context.show_all) context.show_all->begin(ImGui::GetFrameCount());

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
        changed |= drawChildren(*context.tree, context.tree->roots, pack::kNoIndex, false, context);
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
    if (ImGui::Selectable(fonts::label(ICON_FA_BOX_ARCHIVE, "Manage Packs").c_str()) && context.manage_packs) context.manage_packs();
    if (ImGui::Selectable(fonts::label(ICON_FA_GEAR, "Options").c_str()) && context.open_options) context.open_options();
    if (context.open_editor && ImGui::Selectable(fonts::label(ICON_FA_PEN_TO_SQUARE, "Open Editor...").c_str())) context.open_editor();
    if (context.show_all) context.show_all->end();
    return changed;
}

} // namespace viewer::ui::main_menu
