// Select All at the foot of each menu of categories: what it acts on, what its box shows, and
// what a click on it turns them to.

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

#include "viewer/packs/registry.h"
#include "viewer/store.h"
#include "viewer/ui/main_menu.h"

namespace {

using viewer::ui::style::TriState;

// "a" with four children: "a.b" on by default, "a.c" off, a separator, and "a.d" on. With
// `other_first`, a root "z" before it, as another pack merged first leaves the tree.
struct Menu {
    viewer::registry::CategoryTree tree;
    viewer::store::CategoryStates states;
    viewer::ui::main_menu::Context context;

    explicit Menu(bool other_first = false) {
        if (other_first) add("z", pack::kNoIndex);
        const size_t a = tree.nodes.size();
        add("a", pack::kNoIndex);
        add("a.b", a);
        add("a.c", a).default_toggle = false;
        add("a.s", a).is_separator = true;
        add("a.d", a);
        tree.roots = other_first ? std::vector<size_t>{0, a} : std::vector<size_t>{a};
        context.tree = &tree;
        context.states = &states;
    }

    viewer::registry::Category& add(const std::string& id, size_t parent) {
        viewer::registry::Category node;
        node.id = id;
        node.label = id;
        node.parent = parent;
        tree.nodes.push_back(node);
        const size_t index = tree.nodes.size() - 1;
        if (parent != pack::kNoIndex) tree.nodes[parent].children.push_back(index);
        tree.by_id[id] = index;
        return tree.nodes[index];
    }

    std::vector<size_t> targets() const {
        return viewer::ui::main_menu::selectAllTargets(tree, tree.nodes[tree.by_id.at("a")].children, context);
    }
    TriState state() const { return viewer::ui::main_menu::enabledOf(tree, targets(), context); }
    bool on(const std::string& id) const { return states.enabled(id, tree.nodes[tree.by_id.at(id)].default_toggle); }
};

} // namespace

TEST_CASE("Select All shows a dot while some of its menu is on, and turns all of it one way", "[viewer][menu]") {
    Menu menu;
    CHECK(menu.targets() == std::vector<size_t>{1, 2, 4}); // not the separator
    CHECK(menu.state() == TriState::Mixed);

    viewer::ui::main_menu::selectAll(menu.tree, menu.targets(), true, menu.context);
    CHECK(menu.state() == TriState::On);
    CHECK(menu.on("a.c"));

    viewer::ui::main_menu::selectAll(menu.tree, menu.targets(), false, menu.context);
    CHECK(menu.state() == TriState::Off);
    CHECK_FALSE(menu.on("a.b"));
    CHECK_FALSE(menu.on("a.d"));
}

TEST_CASE("Select All acts on its menu's every category, but for what the pack keeps out of it", "[viewer][menu]") {
    Menu menu;

    SECTION("smart categories' hidden ones too") {
        const std::vector<char> on_map = {1, 1, 1, 0, 0}; // "a.d" has nothing here
        menu.context.on_map = &on_map;
        CHECK(menu.targets() == std::vector<size_t>{1, 2, 4});
        viewer::ui::main_menu::selectAll(menu.tree, menu.targets(), false, menu.context);
        CHECK(menu.state() == TriState::Off);
        CHECK_FALSE(menu.on("a.d"));
    }

    SECTION("ishidden leaves a category out of the menu") {
        menu.tree.nodes[2].is_hidden = true;
        CHECK(menu.targets() == std::vector<size_t>{1, 4});
        CHECK(menu.state() == TriState::On);
    }
}

TEST_CASE("a menu whose hidden categories were shown is still the one shown once the tree is built anew", "[viewer][menu][regression]") {
    // A pack removed or read again, as the editor's save reads it, builds the tree anew, which
    // can move every node. Kept by node, the menu shown became whichever category took its place.
    using viewer::ui::main_menu::menuId;
    using viewer::ui::main_menu::showsAll;
    viewer::ui::main_menu::ShowAll shown;

    Menu before;
    before.context.show_all = &shown;
    const size_t moved = before.tree.by_id.at("a.b");
    CHECK_FALSE(showsAll(before.tree, moved, before.context));
    shown.menus.insert(menuId(before.tree, moved)); // as its row's click does
    CHECK(showsAll(before.tree, moved, before.context));

    Menu after(true);
    after.context.show_all = &shown;
    REQUIRE(after.tree.by_id.at("a.b") != moved);
    CHECK(showsAll(after.tree, after.tree.by_id.at("a.b"), after.context));
    CHECK_FALSE(showsAll(after.tree, moved, after.context)); // now "a"

    // All Markers' own menu, which no category is.
    CHECK_FALSE(showsAll(after.tree, pack::kNoIndex, after.context));
    shown.menus.insert(menuId(after.tree, pack::kNoIndex));
    CHECK(showsAll(after.tree, pack::kNoIndex, after.context));
}

TEST_CASE("a menu shows its hidden categories until it closes, as Pathing's does", "[viewer][menu]") {
    viewer::ui::main_menu::ShowAll show_all;
    const std::set<std::string> a = {"a"};

    // Clicked in "a", under All Markers' own menu: kept while "a" is drawn frame after frame.
    show_all.begin(10);
    show_all.drawn("");
    show_all.drawn("a");
    show_all.menus.insert("a");
    show_all.end();
    show_all.begin(11);
    show_all.drawn("");
    show_all.drawn("a");
    show_all.end();
    CHECK(show_all.menus == a);

    SECTION("the mouse leaves \"a\", which closes") {
        show_all.begin(12);
        show_all.drawn("");
        show_all.end();
        CHECK(show_all.menus.empty());
    }

    SECTION("the whole menu closes, and opens again later") {
        show_all.begin(40);
        CHECK(show_all.menus.empty());
    }

    SECTION("the menu drawn twice on one frame") {
        show_all.begin(11);
        CHECK(show_all.menus == a);
    }
}
