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

    // First item selected. The gizmo sits here and a group rotation orbits about it, so it
    // has to stay put as the selection grows — which is why every add appends.
    NodeRef anchor() const { return items_.empty() ? NodeRef{} : items_.front(); }

    void clear() { items_.clear(); }
    void set(NodeRef node);
    void toggle(NodeRef node);
    // Replaces the selection wholesale. The caller's order is kept, so whatever it puts
    // first becomes the anchor.
    void setAll(std::vector<NodeRef> nodes) { items_ = std::move(nodes); }

private:
    std::vector<NodeRef> items_;
};

enum class GizmoMode { Disabled, Translate, Rotate, TranslateRotate };

struct Settings {
    bool highlight_3d = true;
    bool select_in_viewport = true;
    GizmoMode gizmo = GizmoMode::Disabled;
};

struct State {
    pack::Pack pack;
    bool loaded = false;
    bool dirty = false;
    std::string status;

    Settings settings;
    Selection selection;

    // A click in the world asks the tree to open down to the node and scroll it into view.
    // The tree consumes it on its next frame — deferred rather than done directly because
    // the pick arrives from the game's message thread, and because only the tree knows
    // which rows it is drawing.
    bool reveal_pending = false;
    NodeRef reveal_target;
    void requestReveal(NodeRef node) {
        reveal_pending = true;
        reveal_target = node;
    }

    // Pathables grouped by owning category, built once per load. Without this the tree
    // would rescan every pathable for every category it draws — 55k x 14k on the largest
    // pack tested.
    std::vector<std::vector<size_t>> pathables_by_category;
    std::vector<size_t> orphan_pathables; // category could not be resolved

    // Bumped by every edit. The world renderer caches resolved per-marker values and
    // rebuilds when this changes, so it never walks the category chain per frame.
    uint64_t revision = 1;

    // ── Visibility ────────────────────────────────────────────────────────────
    // Purely a view state for the 3D viewport. Never saved, never marks the pack dirty:
    // hiding a category is about looking at the pack, not editing it. (A pack's own
    // defaulttoggle / ishidden attributes are a different thing entirely and do get
    // saved — they live in the attribute table like everything else.)
    std::vector<char> category_hidden;
    std::vector<char> pathable_hidden;

    // Derived: hidden here, or beneath a hidden category.
    std::vector<char> category_visible;
    std::vector<char> pathable_visible;

    bool isHidden(NodeRef node) const;
    bool isVisible(NodeRef node) const;
    void setHidden(NodeRef node, bool hidden);
    void rebuildVisibility();

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
