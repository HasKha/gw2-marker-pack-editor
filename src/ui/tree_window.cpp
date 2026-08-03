#include "ui/tree_window.h"

#include <imgui.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace ui::tree_window {

namespace {

// ── Reveal ────────────────────────────────────────────────────────────────────
// A node picked in the world has to be shown in the tree, which means opening every
// category above it. A parent chain is only walkable upwards, so it is collected once at
// the top of draw() and consulted on the way down. Valid for the one frame that consumes
// State::reveal_pending.
bool reveal_active = false;
editor::NodeRef reveal_target;
std::vector<size_t> reveal_ancestors; // categories to force open, root-first
bool reveal_orphans = false; // the target is a pathable with no resolvable category

void beginReveal(editor::State& state) {
    reveal_active = false;
    reveal_orphans = false;
    reveal_ancestors.clear();
    if (!state.reveal_pending) return;

    state.reveal_pending = false;
    reveal_target = state.reveal_target;
    reveal_active = true;

    size_t category = pack::kNoIndex;
    if (reveal_target.kind == editor::NodeKind::Category) {
        category = reveal_target.index;
    } else if (reveal_target.index < state.pack.pathables.size()) {
        category = state.pack.pathables[reveal_target.index].category;
    }

    if (category == pack::kNoIndex || category >= state.pack.categories.size()) {
        reveal_orphans = reveal_target.kind == editor::NodeKind::Pathable;
        return;
    }

    while (category != pack::kNoIndex && category < state.pack.categories.size()) {
        reveal_ancestors.push_back(category);
        category = state.pack.categories[category].parent;
    }
}

bool revealOpens(size_t category_index) {
    return reveal_active && std::find(reveal_ancestors.begin(), reveal_ancestors.end(), category_index) != reveal_ancestors.end();
}

// Label for a pathable. Packs rarely give markers a title, so fall back to whatever
// identifies it: the trail's data file, the marker's icon, then its position.
std::string pathableLabel(const pack::Pack& pack, size_t index) {
    const pack::Pathable& pathable = pack.pathables[index];

    auto basename = [](const std::string& path) {
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    };

    if (const std::string* title = pathable.attributes.value("title")) {
        if (!title->empty()) return *title;
    }

    if (pathable.type == pack::PathableType::Trail) {
        if (const std::string* data = pack.effectiveValue(pathable, "traildata")) return basename(*data);
        return "Trail";
    }

    const std::string* x = pathable.attributes.value("xpos");
    const std::string* y = pathable.attributes.value("ypos");
    const std::string* z = pathable.attributes.value("zpos");

    std::string suffix;
    if (x && y && z) suffix = " (" + *x + ", " + *y + ", " + *z + ")";

    if (const std::string* icon = pack.effectiveValue(pathable, "iconfile")) {
        if (!icon->empty()) return basename(*icon) + suffix;
    }

    return std::string(pack::toString(pathable.type)) + suffix;
}

// Scene-hierarchy style eye toggle, drawn rather than typed.
//
// FontAwesome would be the obvious source for this, but it cannot reach the atlas: Nexus
// builds every font with its own glyph ranges (Latin, Latin-Ext, and CJK/Cyrillic for the
// default font) and FontAwesome's glyphs live at 0xF000-0xF8FF, so they would be dropped.
// Its FontManager also takes raw TTF while imgui-fonts ships compressed data. Drawing the
// icon sidesteps both.
//
// `dimmed` is for a node that is visible in itself but sits under a hidden ancestor.
bool eyeButton(const char* id, bool open_eye, bool dimmed) {
    const float height = ImGui::GetTextLineHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const bool pressed = ImGui::InvisibleButton(id, ImVec2(height, height));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 centre(origin.x + height * 0.5f, origin.y + height * 0.5f);
    const float w = height * 0.42f; // half-width of the almond
    const float h = height * 0.42f; // bezier control offset, so the lid peaks around 0.75h

    ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (!open_eye || dimmed) colour.w *= 0.35f;
    if (hovered) colour.w = 1.0f;
    const ImU32 packed = ImGui::ColorConvertFloat4ToU32(colour);

    list->PathLineTo(ImVec2(centre.x - w, centre.y));
    list->PathBezierCurveTo(
        ImVec2(centre.x - w * 0.5f, centre.y - h),
        ImVec2(centre.x + w * 0.5f, centre.y - h),
        ImVec2(centre.x + w, centre.y));
    list->PathBezierCurveTo(
        ImVec2(centre.x + w * 0.5f, centre.y + h),
        ImVec2(centre.x - w * 0.5f, centre.y + h),
        ImVec2(centre.x - w, centre.y));
    list->PathStroke(packed, true, 1.4f);

    if (open_eye) {
        list->AddCircleFilled(centre, h * 0.42f, packed);
    } else {
        // Slash across the closed eye, the usual "hidden" affordance.
        list->AddLine(ImVec2(centre.x - w, centre.y + h * 0.8f), ImVec2(centre.x + w, centre.y - h * 0.8f), packed, 1.4f);
    }

    return pressed;
}

// ── Range select ──────────────────────────────────────────────────────────────
// A shift+click range is only meaningful inside one level of the tree: rows in different
// branches are drawn in different places, so "everything between them" would have to
// invent an order across the branches. A level is identified by the group that owns it.
struct RowGroup {
    enum class Kind { Roots, Orphans, Category };
    Kind kind = Kind::Roots;
    size_t category = pack::kNoIndex;

    bool operator==(const RowGroup& other) const { return kind == other.kind && category == other.category; }
};

RowGroup groupOf(const editor::State& state, editor::NodeRef node) {
    if (node.kind == editor::NodeKind::Category) {
        if (node.index >= state.pack.categories.size()) return {RowGroup::Kind::Roots, pack::kNoIndex};
        const size_t parent = state.pack.categories[node.index].parent;
        if (parent == pack::kNoIndex || parent >= state.pack.categories.size()) return {RowGroup::Kind::Roots, pack::kNoIndex};
        return {RowGroup::Kind::Category, parent};
    }

    if (node.index >= state.pack.pathables.size()) return {RowGroup::Kind::Orphans, pack::kNoIndex};
    const size_t category = state.pack.pathables[node.index].category;
    if (category == pack::kNoIndex || category >= state.pack.categories.size()) return {RowGroup::Kind::Orphans, pack::kNoIndex};
    return {RowGroup::Kind::Category, category};
}

// The rows of one level, in the order the tree draws them. Must stay in step with
// drawCategoryRow, or a range would cover rows the user did not see between the ends.
std::vector<editor::NodeRef> rowsOf(const editor::State& state, const RowGroup& group) {
    std::vector<editor::NodeRef> rows;
    switch (group.kind) {
    case RowGroup::Kind::Roots:
        for (size_t root : state.pack.roots) rows.push_back({editor::NodeKind::Category, root});
        break;
    case RowGroup::Kind::Orphans:
        for (size_t pathable : state.orphan_pathables) rows.push_back({editor::NodeKind::Pathable, pathable});
        break;
    case RowGroup::Kind::Category:
        for (size_t child : state.pack.categories[group.category].children) rows.push_back({editor::NodeKind::Category, child});
        for (size_t pathable : state.pathables_by_category[group.category]) rows.push_back({editor::NodeKind::Pathable, pathable});
        break;
    }
    return rows;
}

// Anchor to `node`, inclusive. The anchor is emitted first whichever way the range runs,
// so it stays the selection's anchor and the gizmo does not jump to the other end.
bool selectRange(editor::State& state, editor::NodeRef anchor, editor::NodeRef node) {
    const RowGroup group = groupOf(state, anchor);
    if (!(group == groupOf(state, node))) return false;

    const std::vector<editor::NodeRef> rows = rowsOf(state, group);
    const auto from = std::find(rows.begin(), rows.end(), anchor);
    const auto to = std::find(rows.begin(), rows.end(), node);
    if (from == rows.end() || to == rows.end()) return false;

    std::vector<editor::NodeRef> range;
    if (from <= to) {
        range.assign(from, to + 1);
    } else {
        range.assign(std::make_reverse_iterator(from + 1), std::make_reverse_iterator(to));
    }

    state.selection.setAll(std::move(range));
    return true;
}

void selectNode(editor::State& state, editor::NodeRef node) {
    const ImGuiIO& io = ImGui::GetIO();

    // Shift over a level boundary falls back to a plain select rather than doing nothing,
    // so the click always lands somewhere.
    if (io.KeyShift && !state.selection.empty() && selectRange(state, state.selection.anchor(), node)) return;

    if (io.KeyCtrl) {
        state.selection.toggle(node);
    } else {
        state.selection.set(node);
    }
}

// Draws the eye and applies a click. Returns true when it consumed the click, so the row
// underneath does not also treat it as a selection.
bool drawVisibilityToggle(editor::State& state, editor::NodeRef node) {
    const bool hidden = state.isHidden(node);
    // Visible in itself but suppressed by an ancestor: show an open eye, dimmed, so the
    // node's own setting stays readable.
    const bool dimmed = !hidden && !state.isVisible(node);

    const bool pressed = eyeButton("##vis", !hidden, dimmed);

    if (ImGui::IsItemHovered()) {
        if (dimmed) {
            ImGui::SetTooltip("Hidden by a parent category");
        } else {
            ImGui::SetTooltip(hidden ? "Hidden in the world" : "Visible in the world");
        }
    }

    if (pressed) state.setHidden(node, !hidden);
    return pressed;
}

// Every row is caret, eye, name. The tree node itself carries an empty label so it draws
// only the caret while its hitbox still spans the row; the eye and the name are laid out
// after it with SameLine. SetItemAllowOverlap lets the eye take clicks that would
// otherwise be swallowed by the row.
void drawRow(
    editor::State& state,
    editor::NodeRef node,
    ImGuiTreeNodeFlags flags,
    const std::string& label,
    const ImVec4* label_colour,
    size_t count,
    bool* out_open) {
    const bool open = ImGui::TreeNodeEx("##node", flags);
    ImGui::SetItemAllowOverlap();
    const bool row_clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();

    ImGui::SameLine();
    const bool eye_took_click = drawVisibilityToggle(state, node);

    ImGui::SameLine();
    if (label_colour) ImGui::PushStyleColor(ImGuiCol_Text, *label_colour);
    ImGui::TextUnformatted(label.c_str());
    if (label_colour) ImGui::PopStyleColor();

    if (count > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu)", count);
    }

    if (row_clicked && !eye_took_click) selectNode(state, node);

    // The ancestors were forced open above, so the row exists this frame and can be
    // scrolled to straight away.
    if (reveal_active && node == reveal_target) ImGui::SetScrollHereY(0.5f);

    if (out_open) *out_open = open;
}

void drawPathableRow(editor::State& state, size_t pathable_index) {
    const editor::NodeRef node{editor::NodeKind::Pathable, pathable_index};

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
    if (state.selection.contains(node)) flags |= ImGuiTreeNodeFlags_Selected;

    const bool is_trail = state.pack.pathables[pathable_index].type == pack::PathableType::Trail;
    const ImVec4 trail_colour(0.60f, 0.80f, 1.00f, 1.00f);

    ImGui::PushID(static_cast<int>(pathable_index));
    drawRow(state, node, flags, pathableLabel(state.pack, pathable_index), is_trail ? &trail_colour : nullptr, 0, nullptr);
    ImGui::PopID();
}

void drawCategoryRow(editor::State& state, size_t category_index) {
    const pack::Category& category = state.pack.categories[category_index];
    const editor::NodeRef node{editor::NodeKind::Category, category_index};

    const size_t own_pathables = state.pathables_by_category[category_index].size();
    const bool has_children = !category.children.empty() || own_pathables > 0;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
    if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (state.selection.contains(node)) flags |= ImGuiTreeNodeFlags_Selected;

    // Categories and pathables share an id space here, so keep them apart.
    ImGui::PushID(-1 - static_cast<int>(category_index));

    if (revealOpens(category_index)) ImGui::SetNextItemOpen(true);

    bool open = false;
    drawRow(state, node, flags, category.displayName(), nullptr, own_pathables, &open);

    if (open && has_children) {
        for (size_t child : category.children) drawCategoryRow(state, child);
        for (size_t pathable_index : state.pathables_by_category[category_index]) {
            drawPathableRow(state, pathable_index);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

} // namespace

void draw(editor::State& state) {
    if (!state.loaded) {
        ImGui::TextDisabled("No pack open.");
        return;
    }

    beginReveal(state);

    ImGui::BeginChild("##tree", ImVec2(0, 0), false);

    for (size_t root : state.pack.roots) drawCategoryRow(state, root);

    if (!state.orphan_pathables.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.70f, 0.30f, 1.00f));
        if (reveal_orphans) ImGui::SetNextItemOpen(true);
        const bool open =
            ImGui::TreeNodeEx("##orphans", ImGuiTreeNodeFlags_SpanFullWidth, "Unresolved (%zu)", state.orphan_pathables.size());
        ImGui::PopStyleColor();
        if (open) {
            for (size_t pathable_index : state.orphan_pathables) drawPathableRow(state, pathable_index);
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();
}

} // namespace ui::tree_window
