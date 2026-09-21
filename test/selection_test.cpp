// Selection: the rules every window shares about what a click means and what stops being
// selected when the thing it named goes away. None of this needs imgui, Nexus or a real
// pack — editor::State is plain data and the rules are plain functions over it, which is
// the whole reason they were pulled out of the windows in the first place.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "editor/editor_state.h"

namespace {

using editor::ClickMods;
using editor::NodeKind;
using editor::NodeRef;

NodeRef cat(size_t index) {
    return NodeRef{NodeKind::Category, index};
}

NodeRef path(size_t index) {
    return NodeRef{NodeKind::Pathable, index};
}

// A range resolver over a fixed row order, standing in for the tree's rowRange. Returns
// empty for a node that is not a row, which is how the tree reports "not one level".
std::vector<NodeRef> rowsBetween(const std::vector<NodeRef>& rows, NodeRef anchor, NodeRef to) {
    const auto from = std::find(rows.begin(), rows.end(), anchor);
    const auto until = std::find(rows.begin(), rows.end(), to);
    if (from == rows.end() || until == rows.end()) return {};
    if (from <= until) return std::vector<NodeRef>(from, until + 1);
    return std::vector<NodeRef>(std::make_reverse_iterator(from + 1), std::make_reverse_iterator(until));
}

// A pack with one root category and three markers under it, which is enough for every
// question here: a tombstone keeps its slot, so what matters is that indexes stay put.
editor::State threeMarkers() {
    editor::State state;
    state.pack = pack::createPack("test.xml", "test", "Test");
    REQUIRE(!state.pack.categories.empty());

    for (int i = 0; i < 3; ++i) {
        pack::AttributeCollection attributes;
        attributes.set("type", state.pack.categories[0].id);
        const size_t added = state.pack.addPathable(pack::PathableType::Poi, 0, pack::kNoIndex, 0, std::move(attributes));
        REQUIRE(added != pack::kNoIndex);
    }

    state.rebuildIndex();
    state.loaded = true;
    return state;
}

} // namespace

TEST_CASE("an empty selection reports nothing, not item zero") {
    // The trap this replaced: a default-constructed size_t is point 0, which is a real
    // point. Both element types have to report an index that cannot be mistaken for one.
    editor::Selection nodes;
    CHECK(nodes.empty());
    CHECK(nodes.primary().index == pack::kNoIndex);
    CHECK(nodes.anchor().index == pack::kNoIndex);

    editor::SelectionOf<size_t> points;
    CHECK(points.empty());
    CHECK(points.primary() == pack::kNoIndex);
    CHECK(points.anchor() == pack::kNoIndex);
}

TEST_CASE("the anchor stays put as the selection grows") {
    // The gizmo sits on the anchor and a group rotation orbits about it, so it has to be
    // the first thing selected however many are added afterwards.
    editor::Selection selection;
    selection.set(path(2));
    selection.toggle(path(0));
    selection.toggle(path(1));

    CHECK(selection.anchor() == path(2));
    CHECK(selection.primary() == path(1));
    CHECK(selection.size() == 3);
}

TEST_CASE("a plain click replaces and ctrl toggles") {
    editor::Selection selection;
    const auto none = [](NodeRef, NodeRef) { return std::vector<NodeRef>{}; };

    editor::applyClick(selection, path(1), ClickMods::Replace, none);
    CHECK(selection.size() == 1);

    editor::applyClick(selection, path(2), ClickMods::Toggle, none);
    CHECK(selection.size() == 2);

    // Toggling something already in drops it rather than selecting it twice.
    editor::applyClick(selection, path(1), ClickMods::Toggle, none);
    CHECK(selection.size() == 1);
    CHECK(selection.contains(path(2)));

    editor::applyClick(selection, path(0), ClickMods::Replace, none);
    CHECK(selection.size() == 1);
    CHECK(selection.contains(path(0)));
}

TEST_CASE("a shift click takes the range, anchor first whichever way it runs") {
    const std::vector<NodeRef> rows{path(0), path(1), path(2), path(3)};
    const auto range = [&](NodeRef anchor, NodeRef to) { return rowsBetween(rows, anchor, to); };

    SECTION("downwards") {
        editor::Selection selection;
        selection.set(path(1));
        editor::applyClick(selection, path(3), ClickMods::Range, range);

        CHECK(selection.items() == std::vector<NodeRef>{path(1), path(2), path(3)});
        CHECK(selection.anchor() == path(1));
    }

    SECTION("upwards, and the anchor is still the anchor") {
        editor::Selection selection;
        selection.set(path(2));
        editor::applyClick(selection, path(0), ClickMods::Range, range);

        CHECK(selection.items() == std::vector<NodeRef>{path(2), path(1), path(0)});
        CHECK(selection.anchor() == path(2));
    }
}

TEST_CASE("a shift click that cannot make a range still lands") {
    // Across a level boundary in the tree the resolver comes back empty. Doing nothing
    // would make the click vanish, so it falls back to a plain replace.
    const std::vector<NodeRef> rows{path(0), path(1)};
    editor::Selection selection;
    selection.set(path(0));

    editor::applyClick(selection, cat(7), ClickMods::Range, [&](NodeRef anchor, NodeRef to) { return rowsBetween(rows, anchor, to); });

    CHECK(selection.size() == 1);
    CHECK(selection.contains(cat(7)));
}

TEST_CASE("a shift click on an empty selection has no anchor to run from") {
    editor::Selection selection;
    editor::applyClick(selection, path(1), ClickMods::Range, [](NodeRef, NodeRef) {
        FAIL("the range resolver must not be asked about an empty selection");
        return std::vector<NodeRef>{};
    });

    CHECK(selection.size() == 1);
    CHECK(selection.contains(path(1)));
}

TEST_CASE("clickMods reads shift ahead of ctrl") {
    CHECK(editor::clickMods(false, false) == ClickMods::Replace);
    CHECK(editor::clickMods(true, false) == ClickMods::Toggle);
    CHECK(editor::clickMods(false, true) == ClickMods::Range);
    CHECK(editor::clickMods(true, true) == ClickMods::Range);
}

TEST_CASE("deleting a marker drops only that marker from the selection") {
    editor::State state = threeMarkers();
    state.selection.setAll({path(0), path(1), path(2)});

    state.pack.removePathable(1);
    editor::prune(state);

    CHECK(state.selection.items() == std::vector<NodeRef>{path(0), path(2)});
    // The anchor survives the prune, so the gizmo does not jump to the other end of what
    // is left.
    CHECK(state.selection.anchor() == path(0));
}

TEST_CASE("deleting a category takes its markers out of the selection with it") {
    editor::State state = threeMarkers();
    state.selection.setAll({cat(0), path(0), path(1), path(2)});

    std::vector<size_t> gone;
    for (size_t index : state.pathables_by_category[0]) state.pack.removePathable(index);
    state.pack.removeCategorySubtree(0, &gone);
    editor::prune(state);

    CHECK(state.selection.empty());
}

TEST_CASE("a point selection belongs to one trail and lets go when it changes") {
    editor::PointSelection points;
    CHECK(points.empty());
    CHECK(points.trail == pack::kNoIndex);

    points.openOn(4);
    points.indices.setAll({1, 2, 3});
    CHECK(points.indices.size() == 3);

    // The same trail again — reopening every frame is exactly what the trail editor does,
    // and it must not throw the selection away.
    points.openOn(4);
    CHECK(points.indices.size() == 3);

    // A different trail. The indices meant points of the old one and mean nothing here.
    points.openOn(9);
    CHECK(points.empty());
    CHECK(points.trail == 9);

    points.indices.set(0);
    points.close();
    CHECK(points.empty());
    CHECK(points.trail == pack::kNoIndex);
}

TEST_CASE("pruning points drops the ones past the end of the trail") {
    // What a trail that got shorter behind the selection's back needs — an undo of an
    // "add circle", say. Everything downstream treats a selected index as one that exists.
    editor::PointSelection points;
    points.openOn(0);
    points.indices.setAll({0, 3, 7});

    points.prune(4);

    CHECK(points.indices.items() == std::vector<size_t>{0, 3});
}

TEST_CASE("points and nodes are separate selections") {
    // The bug that split them: selecting a point used to evict its trail, so the trail
    // vanished from the details window and deleting a point read as deselecting the trail.
    editor::State state = threeMarkers();
    const NodeRef trail = path(0);

    state.selection.set(trail);
    state.points.openOn(trail.index);
    state.points.indices.setAll({0, 1});

    // Picking points leaves the node selection alone...
    CHECK(state.selection.primary() == trail);

    // ...and dropping them all leaves it alone too, which is what keeps the trail editor
    // open on the trail after its points are deleted.
    state.points.indices.clear();
    CHECK(state.selection.primary() == trail);
    CHECK(state.points.trail == trail.index);
}
