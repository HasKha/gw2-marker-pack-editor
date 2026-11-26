#pragma once

#include <cstddef>

#include "pack/model.h"

namespace editor {

enum class NodeKind { Category, Pathable };

// A selectable node of the tree. `index` is into Pack::categories or Pack::pathables.
struct NodeRef {
    NodeKind kind = NodeKind::Category;
    size_t index = pack::kNoIndex;

    bool operator==(const NodeRef& other) const = default;
};

} // namespace editor
