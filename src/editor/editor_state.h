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

// An ordered set of selected things. Ordered so bulk edits apply in a predictable sequence,
// and so the first and last clicked items can act as the anchor and the primary.
//
// Templated because the editor selects two unrelated kinds of thing — tree nodes and the
// points of a trail — and the container is the same for both. What differs is only what an
// element is; see PointSelection for why they are not one list.
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

// The selected points of the trail open in the trail editor.
//
// Deliberately not part of `Selection`. A trail point is not a sibling of a marker in the
// tree; it is a sub-selection *inside* a trail that is itself selected. Folding the two into
// one list meant clicking a point evicted its trail from the details panel, made deleting a
// point look like deselecting the trail, and left every consumer filtering the mixture back
// apart by kind. Keeping them apart also carries the trail index once, here, rather than on
// every entry.
//
// The invariant every consumer relies on: `indices` only ever holds points that exist, in
// the trail `trail` names. It is maintained here and in trail_edit, which is the only thing
// that can know a trail's shape — so nothing downstream has to bounds-check a point again.
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

    // Takes a newly created pathable into the editor's indexes. `after` is the sibling it
    // should follow in the tree, or kNoIndex to put it last in its category.
    //
    // Order in the tree comes from `pathables_by_category`, not from the order of
    // `Pack::pathables` — which is exactly why a new marker can be appended to the pack and
    // still show up in the middle of a category. Appending is what keeps every existing
    // index, selection entry and cache slot valid.
    void pathableAdded(size_t index, size_t after);

    // The same for a new category. Its place in the tree comes from the model's
    // parent/children, so all this has to do is grow the per-category vectors alongside it.
    void categoryAdded(size_t index);

    void reset();

    // Marks the pack edited. Every mutation goes through here so the title bar, the save
    // prompt, the render cache and (later) undo all see the same signal.
    void touch() {
        dirty = true;
        ++revision;
    }
};

// The attributes a node owns, or nullptr for a stale index. Lives here rather than in
// whichever window needed it first, because by now three of them do.
pack::AttributeCollection* attributesOf(State& state, NodeRef node);
const pack::AttributeCollection* attributesOf(const State& state, NodeRef node);

// True for a node that has been deleted. Tombstones keep their slot, so an index alone
// does not tell you whether the thing is still there.
bool isDeleted(const State& state, NodeRef node);

// Drops selected nodes that no longer name anything. The one place that checks, run after
// every structural edit, so no consumer has to defend itself against a stale entry.
//
// Points are pruned by trail_edit instead: only it knows a trail's shape.
void prune(State& state);

} // namespace editor
