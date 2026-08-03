#include "ui/tree_window.h"

#include <imgui.h>

#include <string>

namespace ui::tree_window {

namespace {

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

void selectNode(editor::State& state, editor::NodeRef node) {
    if (ImGui::GetIO().KeyCtrl) {
        state.selection.toggle(node);
    } else {
        state.selection.set(node);
    }
}

void drawPathableLeaf(editor::State& state, size_t pathable_index) {
    const editor::NodeRef node{editor::NodeKind::Pathable, pathable_index};

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
    if (state.selection.contains(node)) flags |= ImGuiTreeNodeFlags_Selected;

    const bool is_trail = state.pack.pathables[pathable_index].type == pack::PathableType::Trail;
    if (is_trail) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.80f, 1.00f, 1.00f));

    ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<intptr_t>(pathable_index + 1)),
        flags,
        "%s",
        pathableLabel(state.pack, pathable_index).c_str());

    if (is_trail) ImGui::PopStyleColor();

    if (ImGui::IsItemClicked()) selectNode(state, node);
}

void drawCategory(editor::State& state, size_t category_index) {
    const pack::Category& category = state.pack.categories[category_index];
    const editor::NodeRef node{editor::NodeKind::Category, category_index};

    const size_t own_pathables = state.pathables_by_category[category_index].size();
    const bool has_children = !category.children.empty() || own_pathables > 0;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
    if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (state.selection.contains(node)) flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(static_cast<int>(category_index));
    const bool open = ImGui::TreeNodeEx("##cat", flags, "%s", category.displayName().c_str());

    // Clicking the arrow toggles; clicking the label selects.
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selectNode(state, node);

    if (own_pathables > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu)", own_pathables);
    }

    if (open && has_children) {
        for (size_t child : category.children) drawCategory(state, child);
        for (size_t pathable_index : state.pathables_by_category[category_index]) {
            drawPathableLeaf(state, pathable_index);
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

    ImGui::BeginChild("##tree", ImVec2(0, 0), false);

    for (size_t root : state.pack.roots) drawCategory(state, root);

    if (!state.orphan_pathables.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.70f, 0.30f, 1.00f));
        const bool open =
            ImGui::TreeNodeEx("##orphans", ImGuiTreeNodeFlags_SpanFullWidth, "Unresolved (%zu)", state.orphan_pathables.size());
        ImGui::PopStyleColor();
        if (open) {
            for (size_t pathable_index : state.orphan_pathables) drawPathableLeaf(state, pathable_index);
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();
}

} // namespace ui::tree_window
