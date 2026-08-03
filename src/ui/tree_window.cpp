#include "ui/tree_window.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace ui::tree_window {

namespace {

char filter_buffer[128] = {};

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

bool matchesFilter(const std::string& text, const std::string& needle) {
    if (needle.empty()) return true;
    std::string lowered;
    lowered.reserve(text.size());
    for (char c : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return lowered.find(needle) != std::string::npos;
}

// A category is worth drawing if it or anything beneath it matches. Without this, typing
// in the filter would hide every parent and so hide the matches too.
//
// Computed once per filter change rather than per frame: evaluating it live is O(n x
// subtree) every frame, which on the largest pack tested (14,430 categories, 55,887
// markers) is far too slow to run inside the render callback.
std::string cached_needle = "\x01"; // impossible value, so the first frame always builds
std::vector<char> subtree_match;

void rebuildFilterCache(const editor::State& state, const std::string& needle) {
    const size_t count = state.pack.categories.size();
    subtree_match.assign(count, needle.empty() ? 1 : 0);
    if (needle.empty()) return;

    // A child is always created after its parent, so reverse index order visits children
    // before the parent that needs their result.
    for (size_t i = count; i-- > 0;) {
        const pack::Category& category = state.pack.categories[i];

        bool matched = matchesFilter(category.displayName(), needle) || matchesFilter(category.id, needle);

        if (!matched) {
            for (size_t pathable : state.pathables_by_category[i]) {
                if (matchesFilter(pathableLabel(state.pack, pathable), needle)) {
                    matched = true;
                    break;
                }
            }
        }

        if (!matched) {
            for (size_t child : category.children) {
                if (child < count && subtree_match[child]) {
                    matched = true;
                    break;
                }
            }
        }

        subtree_match[i] = matched ? 1 : 0;
    }
}

bool subtreeMatches(size_t category_index) {
    return category_index < subtree_match.size() ? subtree_match[category_index] != 0 : true;
}

void selectNode(editor::State& state, editor::NodeRef node) {
    if (ImGui::GetIO().KeyCtrl) {
        state.selection.toggle(node);
    } else {
        state.selection.set(node);
    }
}

void drawPathables(editor::State& state, size_t category_index, const std::string& needle) {
    const std::vector<size_t>& pathables = state.pathables_by_category[category_index];

    for (size_t pathable_index : pathables) {
        const std::string label = pathableLabel(state.pack, pathable_index);
        if (!matchesFilter(label, needle)) continue;

        const editor::NodeRef node{editor::NodeKind::Pathable, pathable_index};

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
        if (state.selection.contains(node)) flags |= ImGuiTreeNodeFlags_Selected;

        const pack::Pathable& pathable = state.pack.pathables[pathable_index];
        const bool is_trail = pathable.type == pack::PathableType::Trail;
        if (is_trail) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.80f, 1.00f, 1.00f));

        ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(pathable_index + 1)), flags, "%s", label.c_str());

        if (is_trail) ImGui::PopStyleColor();

        if (ImGui::IsItemClicked()) selectNode(state, node);
    }
}

void drawCategory(editor::State& state, size_t category_index, const std::string& needle) {
    if (!subtreeMatches(category_index)) return;

    const pack::Category& category = state.pack.categories[category_index];
    const editor::NodeRef node{editor::NodeKind::Category, category_index};

    const size_t own_pathables = state.pathables_by_category[category_index].size();
    const bool has_children = !category.children.empty() || own_pathables > 0;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
    if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (state.selection.contains(node)) flags |= ImGuiTreeNodeFlags_Selected;
    // While filtering, open everything so matches deep in the tree are actually visible.
    if (!needle.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);

    ImGui::PushID(static_cast<int>(category_index));
    const bool open = ImGui::TreeNodeEx("##cat", flags, "%s", category.displayName().c_str());

    // Clicking the arrow toggles; clicking the label selects.
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selectNode(state, node);

    if (own_pathables > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu)", own_pathables);
    }

    if (open && has_children) {
        for (size_t child : category.children) drawCategory(state, child, needle);
        drawPathables(state, category_index, needle);
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

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter...", filter_buffer, sizeof(filter_buffer));

    std::string needle;
    for (const char* c = filter_buffer; *c; ++c) needle.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*c))));

    if (needle != cached_needle || subtree_match.size() != state.pack.categories.size()) {
        rebuildFilterCache(state, needle);
        cached_needle = needle;
    }

    ImGui::BeginChild("##tree", ImVec2(0, 0), false);

    for (size_t root : state.pack.roots) drawCategory(state, root, needle);

    if (!state.orphan_pathables.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.70f, 0.30f, 1.00f));
        const bool open =
            ImGui::TreeNodeEx("##orphans", ImGuiTreeNodeFlags_SpanFullWidth, "Unresolved (%zu)", state.orphan_pathables.size());
        ImGui::PopStyleColor();
        if (open) {
            for (size_t pathable_index : state.orphan_pathables) {
                const std::string label = pathableLabel(state.pack, pathable_index);
                if (!matchesFilter(label, needle)) continue;

                const editor::NodeRef node{editor::NodeKind::Pathable, pathable_index};
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
                if (state.selection.contains(node)) flags |= ImGuiTreeNodeFlags_Selected;

                ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(pathable_index + 1)), flags, "%s", label.c_str());
                if (ImGui::IsItemClicked()) selectNode(state, node);
            }
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();
}

} // namespace ui::tree_window
