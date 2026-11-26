#include "editor/ui/tree_window.h"
#include "viewer/ui/fonts.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace ui::tree_window {

namespace {

// Label for a pathable. Packs rarely give markers a title, so fall back to whatever
// identifies it: the trail's data file, the marker's icon, then its position.
std::string pathableLabel(const pack::Pack& pack, size_t index) {
    if (index >= pack.pathables.size()) return "(stale)";

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

// Scene-hierarchy style eye toggle: Font Awesome's eye, through our own font (viewer/ui/fonts.*),
// and drawn by hand where that font is not built.
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

    if (viewer::ui::fonts::ready()) {
        const char* glyph = open_eye ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
        const ImVec2 size = ImGui::CalcTextSize(glyph);
        list->AddText(ImVec2(centre.x - size.x * 0.5f, centre.y - size.y * 0.5f), packed, glyph);
        return pressed;
    }

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
// drawCategoryRow, or a range would cover rows the user did not see between the ends —
// which is why deleted entries are filtered out here as well.
std::vector<editor::NodeRef> rowsOf(const editor::State& state, const RowGroup& group) {
    std::vector<editor::NodeRef> rows;

    auto addCategory = [&](size_t index) {
        if (index < state.pack.categories.size() && !state.pack.categories[index].grave.deleted) {
            rows.push_back({editor::NodeKind::Category, index});
        }
    };
    auto addPathable = [&](size_t index) {
        if (index < state.pack.pathables.size() && !state.pack.pathables[index].grave.deleted) {
            rows.push_back({editor::NodeKind::Pathable, index});
        }
    };

    switch (group.kind) {
    case RowGroup::Kind::Roots:
        for (size_t root : state.pack.roots) addCategory(root);
        break;
    case RowGroup::Kind::Orphans:
        for (size_t pathable : state.orphan_pathables) addPathable(pathable);
        break;
    case RowGroup::Kind::Category:
        for (size_t child : state.pack.categories[group.category].children) addCategory(child);
        for (size_t pathable : state.pathables_by_category[group.category]) addPathable(pathable);
        break;
    }
    return rows;
}

// Anchor to `node`, inclusive, or empty when the two are not rows of one level — which
// applyClick turns into a plain select, so a shift over a level boundary still lands
// somewhere. The anchor is emitted first whichever way the range runs, so it stays the
// selection's anchor and the gizmo does not jump to the other end.
std::vector<editor::NodeRef> rowRange(const editor::State& state, editor::NodeRef anchor, editor::NodeRef node) {
    const RowGroup group = groupOf(state, anchor);
    if (!(group == groupOf(state, node))) return {};

    const std::vector<editor::NodeRef> rows = rowsOf(state, group);
    const auto from = std::find(rows.begin(), rows.end(), anchor);
    const auto to = std::find(rows.begin(), rows.end(), node);
    if (from == rows.end() || to == rows.end()) return {};

    if (from <= to) return std::vector<editor::NodeRef>(from, to + 1);
    return std::vector<editor::NodeRef>(std::make_reverse_iterator(from + 1), std::make_reverse_iterator(to));
}

void selectNode(editor::State& state, editor::NodeRef node) {
    const ImGuiIO& io = ImGui::GetIO();

    editor::applyClick(state.selection, node, editor::clickMods(io.KeyCtrl, io.KeyShift), [&](editor::NodeRef anchor, editor::NodeRef to) {
        return rowRange(state, anchor, to);
    });
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
    const Reveal& reveal,
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
    if (reveal.active && node == reveal.target) ImGui::SetScrollHereY(0.5f);

    if (out_open) *out_open = open;
}

void drawPathableRow(editor::State& state, const Reveal& reveal, size_t pathable_index) {
    if (state.pack.pathables[pathable_index].grave.deleted) return;

    const editor::NodeRef node{editor::NodeKind::Pathable, pathable_index};

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
    if (state.selection.contains(node)) flags |= ImGuiTreeNodeFlags_Selected;

    const bool is_trail = state.pack.pathables[pathable_index].type == pack::PathableType::Trail;
    const ImVec4 trail_colour(0.60f, 0.80f, 1.00f, 1.00f);

    ImGui::PushID(static_cast<int>(pathable_index));
    drawRow(state, reveal, node, flags, pathableLabel(state.pack, pathable_index), is_trail ? &trail_colour : nullptr, 0, nullptr);
    ImGui::PopID();
}

void drawCategoryRow(editor::State& state, const Reveal& reveal, size_t category_index) {
    const pack::Category& category = state.pack.categories[category_index];
    if (category.grave.deleted) return;

    const editor::NodeRef node{editor::NodeKind::Category, category_index};

    // Counted rather than taken from the list's size: a deleted marker keeps its slot in
    // it, and a category showing "(3)" over two rows would be a lie.
    size_t own_pathables = 0;
    for (size_t index : state.pathables_by_category[category_index]) {
        if (!state.pack.pathables[index].grave.deleted) ++own_pathables;
    }

    bool has_children = own_pathables > 0;
    for (size_t child : category.children) {
        has_children = has_children || !state.pack.categories[child].grave.deleted;
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
    if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (state.selection.contains(node)) flags |= ImGuiTreeNodeFlags_Selected;

    // Categories and pathables share an id space here, so keep them apart.
    ImGui::PushID(-1 - static_cast<int>(category_index));

    if (reveal.opens(category_index)) ImGui::SetNextItemOpen(true);

    bool open = false;
    drawRow(state, reveal, node, flags, category.displayName(), nullptr, own_pathables, &open);

    if (open && has_children) {
        for (size_t child : category.children) drawCategoryRow(state, reveal, child);
        for (size_t pathable_index : state.pathables_by_category[category_index]) {
            drawPathableRow(state, reveal, pathable_index);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

} // namespace

std::string nodePath(const pack::Pack& pack, editor::NodeRef node) {
    if (node.kind == editor::NodeKind::Category) {
        return node.index < pack.categories.size() ? pack.categories[node.index].id : "(stale)";
    }
    if (node.index >= pack.pathables.size()) return "(stale)";

    const std::string& type = pack.pathables[node.index].category_id;
    return type.empty() ? "(no type)" : type;
}

bool Reveal::opens(size_t category) const {
    return active && std::find(ancestors.begin(), ancestors.end(), category) != ancestors.end();
}

Reveal update(editor::State& state) {
    Reveal reveal;
    if (!state.reveal_pending) return reveal;

    state.reveal_pending = false;
    reveal.target = state.reveal_target;
    reveal.active = true;

    size_t category = pack::kNoIndex;
    if (reveal.target.kind == editor::NodeKind::Category) {
        category = reveal.target.index;
    } else if (reveal.target.index < state.pack.pathables.size()) {
        category = state.pack.pathables[reveal.target.index].category;
    }

    if (category == pack::kNoIndex || category >= state.pack.categories.size()) {
        reveal.orphans = reveal.target.kind == editor::NodeKind::Pathable;
        return reveal;
    }

    while (category != pack::kNoIndex && category < state.pack.categories.size()) {
        reveal.ancestors.push_back(category);
        category = state.pack.categories[category].parent;
    }
    return reveal;
}

void draw(editor::State& state, const Reveal& reveal) {
    if (!state.loaded) {
        ImGui::TextDisabled("No pack open.");
        return;
    }

    ImGui::BeginChild("##tree", ImVec2(0, 0), false);

    for (size_t root : state.pack.roots) drawCategoryRow(state, reveal, root);

    size_t orphans = 0;
    for (size_t index : state.orphan_pathables) {
        if (!state.pack.pathables[index].grave.deleted) ++orphans;
    }

    if (orphans > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.70f, 0.30f, 1.00f));
        if (reveal.orphans) ImGui::SetNextItemOpen(true);
        const bool open = ImGui::TreeNodeEx("##orphans", ImGuiTreeNodeFlags_SpanFullWidth, "Unresolved (%zu)", orphans);
        ImGui::PopStyleColor();
        if (open) {
            for (size_t pathable_index : state.orphan_pathables) drawPathableRow(state, reveal, pathable_index);
            ImGui::TreePop();
        }
    }

    // A click in the tree's blank space clears the selection — the other half of being able
    // to select something. Every row spans the full width, so "blank" is the space past the
    // last row, and IsAnyItemHovered is what tells it from a row: the child being hovered
    // says the click was inside this box, and no item being hovered says it missed them all.
    //
    // Not with ctrl or shift held, the same as in the world: those mean "add to what I have",
    // and a miss should not discard the selection they are being held to extend.
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(0) && !io.KeyCtrl && !io.KeyShift) {
        state.selection.clear();
    }

    ImGui::EndChild();
}

} // namespace ui::tree_window
