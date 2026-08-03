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
}

void State::reset() {
    pack = pack::Pack{};
    loaded = false;
    dirty = false;
    status.clear();
    selection.clear();
    pathables_by_category.clear();
    orphan_pathables.clear();
}

} // namespace editor
