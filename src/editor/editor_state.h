#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "pack/model.h"

namespace editor {

enum class NodeKind { Category, Pathable };

// A selectable node of the tree. `index` is into Pack::categories or Pack::pathables.
struct NodeRef {
    NodeKind kind = NodeKind::Category;
    size_t index = pack::kNoIndex;

    bool operator==(const NodeRef& other) const { return kind == other.kind && index == other.index; }
};

// An ordered set of selected things, so bulk edits apply in a predictable sequence and the first and
// last clicked can be the anchor and the primary. Used for tree nodes and trail points alike.
template <typename T>
class SelectionOf
{
public:
    // What primary() and anchor() report when nothing is selected. Specialised per element
    // type because a default-constructed size_t is point 0 — a real point — where a
    // default-constructed NodeRef names nothing.
    static T none();

    const std::vector<T>& items() const { return items_; }
    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }
    bool contains(const T& item) const { return std::find(items_.begin(), items_.end(), item) != items_.end(); }

    // Last item clicked. Drives the property panel when several things are selected.
    T primary() const { return items_.empty() ? none() : items_.back(); }

    // First item selected. The gizmo sits here and a group rotation orbits about it, so it
    // has to stay put as the selection grows — which is why every add appends.
    T anchor() const { return items_.empty() ? none() : items_.front(); }

    void clear() { items_.clear(); }

    void set(const T& item) {
        items_.clear();
        items_.push_back(item);
    }

    void toggle(const T& item) {
        auto it = std::find(items_.begin(), items_.end(), item);
        if (it != items_.end()) {
            items_.erase(it);
        } else {
            items_.push_back(item);
        }
    }

    // Drops entries the predicate rejects. What a structural edit needs: a node it has just
    // un-created is not something the user can still have selected.
    template <typename Predicate>
    void retainIf(Predicate keep) {
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const T& item) { return !keep(item); }), items_.end());
    }

    // Replaces the selection wholesale. The caller's order is kept, so whatever it puts
    // first becomes the anchor.
    void setAll(std::vector<T> items) { items_ = std::move(items); }

private:
    std::vector<T> items_;
};

template <>
inline NodeRef SelectionOf<NodeRef>::none() {
    return NodeRef{};
}

template <>
inline size_t SelectionOf<size_t>::none() {
    return pack::kNoIndex;
}

using Selection = SelectionOf<NodeRef>;

// The selected points of the open trail: a sub-selection inside a selected trail, so not part of
// `Selection`. `indices` only ever holds points that exist in `trail`, kept so here and by
// trail_edit, the only thing that knows a trail's shape.
struct PointSelection {
    size_t trail = pack::kNoIndex; // the pathable, or kNoIndex when no trail is open
    SelectionOf<size_t> indices;

    bool empty() const { return indices.empty(); }

    // Points at `trail_index`, dropping what was selected when it is a different trail.
    void openOn(size_t trail_index) {
        if (trail == trail_index) return;
        trail = trail_index;
        indices.clear();
    }

    void close() {
        trail = pack::kNoIndex;
        indices.clear();
    }

    // Drops indices past the end of a trail that has just changed shape.
    void prune(size_t point_count) {
        indices.retainIf([&](size_t index) { return index < point_count; });
    }
};

// What a click's modifiers mean. One rule for every list that selects, so the tree and the
// trail's point list cannot drift apart on what ctrl and shift do.
enum class ClickMods { Replace, Toggle, Range };

inline ClickMods clickMods(bool ctrl, bool shift) {
    if (shift) return ClickMods::Range;
    return ctrl ? ClickMods::Toggle : ClickMods::Replace;
}

// Applies `mods` to `selection`. `resolveRange` is handed the anchor and the clicked item
// and returns the run between them in the order it wants them selected — anchor first, so
// the gizmo does not jump to the other end — or an empty vector when the two do not share a
// range. A shift click that cannot make one falls back to a plain replace, so the click
// always lands somewhere.
template <typename SelectionT, typename ItemT, typename RangeFn>
void applyClick(SelectionT& selection, const ItemT& item, ClickMods mods, RangeFn resolveRange) {
    if (mods == ClickMods::Range && !selection.empty()) {
        std::vector<ItemT> range = resolveRange(selection.anchor(), item);
        if (!range.empty()) {
            selection.setAll(std::move(range));
            return;
        }
    }

    if (mods == ClickMods::Toggle) {
        selection.toggle(item);
    } else {
        selection.set(item);
    }
}

// For a list with no ranges of its own — the viewport, where there are no rows to run
// between.
template <typename SelectionT, typename ItemT>
void applyClick(SelectionT& selection, const ItemT& item, ClickMods mods) {
    applyClick(selection, item, mods, [](const ItemT&, const ItemT&) { return std::vector<ItemT>{}; });
}

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

    // Categories and pathables — what the tree shows and the details window edits.
    Selection selection;
    // Points of the trail open in the editor. Independent of `selection`: a trail stays
    // selected, and stays in the details window, while its points are picked over.
    PointSelection points;

    // A click in the world asks the tree to open down to the node and scroll to it; the tree
    // consumes it next frame, since only it knows which rows it draws.
    bool reveal_pending = false;
    NodeRef reveal_target;
    void requestReveal(NodeRef node) {
        reveal_pending = true;
        reveal_target = node;
    }

    // Pathables grouped by owning category, in tree order, so the tree does not rescan every
    // pathable per category.
    std::vector<std::vector<size_t>> pathables_by_category;
    std::vector<size_t> orphan_pathables; // category could not be resolved

    // Bumped by every edit. The viewer bakes the pack's resolved per-marker values and
    // rebuilds when this changes, so it never walks the category chain per frame.
    uint64_t revision = 1;

    // ── Visibility ────────────────────────────────────────────────────────────
    // View state only: never saved, never marks the pack dirty. Unrelated to the pack's own
    // defaulttoggle / ishidden.
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

    // Takes a newly created pathable into the editor's indexes. `after` is the sibling it follows in
    // the tree, or kNoIndex to put it last in its category.
    void pathableAdded(size_t index, size_t after);

    // The same for a new category, whose tree place is the model's: this grows the per-category
    // vectors.
    void categoryAdded(size_t index);

    void reset();

    // Marks the pack edited and bumps the revision. Every mutation goes through here.
    void touch() {
        dirty = true;
        ++revision;
    }
};

// The attributes a node owns, or nullptr for a stale index.
pack::AttributeCollection* attributesOf(State& state, NodeRef node);
const pack::AttributeCollection* attributesOf(const State& state, NodeRef node);

// True for a node that has been deleted: tombstones keep their slot.
bool isDeleted(const State& state, NodeRef node);

// Drops selected nodes that no longer name anything. The one place that checks, run after
// every structural edit, so no consumer has to defend itself against a stale entry.
//
// Points are pruned by trail_edit instead: only it knows a trail's shape.
void prune(State& state);

} // namespace editor
