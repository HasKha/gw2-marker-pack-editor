#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "pack/model.h"

namespace editor {

enum class NodeKind { Category, Pathable };

struct NodeRef {
    NodeKind kind = NodeKind::Category;
    size_t index = pack::kNoIndex;

    bool operator==(const NodeRef& other) const { return kind == other.kind && index == other.index; }
};

// Ordered so bulk edits apply in a predictable sequence, and so the last-clicked item can
// act as the anchor for shift-select.
class Selection
{
public:
    const std::vector<NodeRef>& items() const { return items_; }
    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }
    bool contains(NodeRef node) const;

    // Last item clicked. Drives the property panel when several things are selected.
    NodeRef primary() const { return items_.empty() ? NodeRef{} : items_.back(); }

    void clear() { items_.clear(); }
    void set(NodeRef node);
    void toggle(NodeRef node);

private:
    std::vector<NodeRef> items_;
};

struct State {
    pack::Pack pack;
    bool loaded = false;
    bool dirty = false;
    std::string status;

    Selection selection;

    // Pathables grouped by owning category, built once per load. Without this the tree
    // would rescan every pathable for every category it draws — 55k x 14k on the largest
    // pack tested.
    std::vector<std::vector<size_t>> pathables_by_category;
    std::vector<size_t> orphan_pathables; // category could not be resolved

    // Bumped by every edit. The world renderer caches resolved per-marker values and
    // rebuilds when this changes, so it never walks the category chain per frame.
    uint64_t revision = 1;

    void rebuildIndex();
    void reset();

    // Marks the pack edited. Every mutation goes through here so the title bar, the save
    // prompt, the render cache and (later) undo all see the same signal.
    void touch() {
        dirty = true;
        ++revision;
    }
};

} // namespace editor
