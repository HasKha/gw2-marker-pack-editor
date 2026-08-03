#include "editor_state.h"

#include <algorithm>

namespace editor {

bool Selection::contains(NodeRef node) const {
    return std::find(items_.begin(), items_.end(), node) != items_.end();
}

void Selection::set(NodeRef node) {
    items_.clear();
    items_.push_back(node);
}

void Selection::toggle(NodeRef node) {
    auto it = std::find(items_.begin(), items_.end(), node);
    if (it != items_.end()) {
        items_.erase(it);
    } else {
        items_.push_back(node);
    }
}

void State::rebuildIndex() {
    pathables_by_category.assign(pack.categories.size(), {});
    orphan_pathables.clear();

    for (size_t i = 0; i < pack.pathables.size(); ++i) {
        const size_t category = pack.pathables[i].category;
        if (category == pack::kNoIndex || category >= pathables_by_category.size()) {
            orphan_pathables.push_back(i);
        } else {
            pathables_by_category[category].push_back(i);
        }
    }

    category_hidden.assign(pack.categories.size(), 0);
    pathable_hidden.assign(pack.pathables.size(), 0);
    rebuildVisibility();
}

void State::pathableAdded(size_t index, size_t after) {
    if (index >= pack.pathables.size()) return;

    // The new entry is at the end of Pack::pathables, so these line up again by growing.
    pathable_hidden.resize(pack.pathables.size(), 0);

    const size_t category = pack.pathables[index].category;
    const bool orphan = category == pack::kNoIndex || category >= pathables_by_category.size();
    std::vector<size_t>& siblings = orphan ? orphan_pathables : pathables_by_category[category];

    auto at = std::find(siblings.begin(), siblings.end(), after);
    if (at == siblings.end()) {
        siblings.push_back(index);
    } else {
        siblings.insert(at + 1, index);
    }

    rebuildVisibility();
    touch();
}

// Visibility is a property of tree rows. A trail point is not one, so it answers "shown,
// not hidden" rather than indexing a vector it has no place in.
bool State::isHidden(NodeRef node) const {
    if (node.kind == NodeKind::Category) {
        return node.index < category_hidden.size() && category_hidden[node.index] != 0;
    }
    if (node.kind != NodeKind::Pathable) return false;
    return node.index < pathable_hidden.size() && pathable_hidden[node.index] != 0;
}

bool State::isVisible(NodeRef node) const {
    if (node.kind == NodeKind::Category) {
        return node.index >= category_visible.size() || category_visible[node.index] != 0;
    }
    if (node.kind != NodeKind::Pathable) return true;
    return node.index >= pathable_visible.size() || pathable_visible[node.index] != 0;
}

void State::setHidden(NodeRef node, bool hidden) {
    if (node.kind == NodeKind::Category) {
        if (node.index >= category_hidden.size()) return;
        category_hidden[node.index] = hidden ? 1 : 0;
    } else if (node.kind == NodeKind::Pathable) {
        if (node.index >= pathable_hidden.size()) return;
        pathable_hidden[node.index] = hidden ? 1 : 0;
    } else {
        return;
    }
    rebuildVisibility();
}

void State::rebuildVisibility() {
    category_visible.assign(pack.categories.size(), 1);
    pathable_visible.assign(pack.pathables.size(), 1);

    // A child is always created after its parent, so a single forward pass resolves the
    // whole tree — the parent's result is already final by the time a child reads it.
    for (size_t i = 0; i < pack.categories.size(); ++i) {
        const size_t parent = pack.categories[i].parent;
        const bool parent_visible = parent == pack::kNoIndex || category_visible[parent] != 0;
        const bool self_hidden = i < category_hidden.size() && category_hidden[i] != 0;
        category_visible[i] = (parent_visible && !self_hidden) ? 1 : 0;
    }

    for (size_t i = 0; i < pack.pathables.size(); ++i) {
        const size_t category = pack.pathables[i].category;
        const bool category_ok = category == pack::kNoIndex || category >= category_visible.size() || category_visible[category] != 0;
        const bool self_hidden = i < pathable_hidden.size() && pathable_hidden[i] != 0;
        pathable_visible[i] = (category_ok && !self_hidden) ? 1 : 0;
    }
}

void State::reset() {
    // Caches keyed on the revision — the renderer's, the resources window's — would
    // otherwise survive into the next pack, since `this` does not move between packs.
    ++revision;

    pack = pack::Pack{};
    loaded = false;
    dirty = false;
    status.clear();
    selection.clear();
    reveal_pending = false;
    reveal_target = NodeRef{};
    pathables_by_category.clear();
    orphan_pathables.clear();
    category_hidden.clear();
    pathable_hidden.clear();
    category_visible.clear();
    pathable_visible.clear();
}

} // namespace editor
