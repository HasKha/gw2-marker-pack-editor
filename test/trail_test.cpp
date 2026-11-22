// Creating and editing trails, above the pack layer: pathable_edit places them, trail_edit
// holds the working copy the editor window and the gizmo both drive. Neither needs a game,
// and "two trails in one category" is exactly the thing that has to keep working.
#include <catch2/catch_test_macros.hpp>

#include "corpus.h"
#include "editor/editor_state.h"
#include "editor/pathable_edit.h"
#include "editor/resources.h"
#include "editor/trail_edit.h"
#include "editor/undo.h"
#include "pack/model.h"
#include "pack/xml_reader.h"

namespace {

constexpr int32_t kMapId = 15;

// A pack with nothing in it but one root category, the way New Pack builds it. No source:
// everything it holds is a pending asset, which is the state a freshly created trail is in.
void startPack(editor::State& state) {
    undo::clear();
    resources::setSource(nullptr);
    resources::setPendingAssets(&state.pack);
    resources::beginFrame();

    state.pack = pack::createPack("markers.xml", "testpack", "Test Pack");
    REQUIRE_FALSE(state.pack.categories.empty());
    state.rebuildIndex();
    state.loaded = true;
    state.selection.set(editor::NodeRef{editor::NodeKind::Category, 0});
}

std::string trailDataOf(const editor::State& state, size_t pathable) {
    const std::string* value = state.pack.effectiveValue(state.pack.pathables[pathable], "traildata");
    return value ? *value : std::string();
}

// What marker_editor::render does every frame, minus the drawing: refill the decode budget
// and let the open trail follow the selection.
void frame(editor::State& state) {
    resources::beginFrame();
    undo::beginFrame();
    trail_edit::followSelection(state);
}

} // namespace

TEST_CASE("two trails in one category get their own .trl", "[trail]") {
    editor::State state;
    startPack(state);

    const size_t first = pathable_edit::addTrail(state, kMapId);
    REQUIRE(first != pack::kNoIndex);

    // Straight after the first, which is what the toolbar does: adding a trail selects it,
    // so the next Add Trail is made with a trail selected rather than a category.
    const size_t second = pathable_edit::addTrail(state, kMapId);
    REQUIRE(second != pack::kNoIndex);
    REQUIRE(first != second);

    const std::string first_data = trailDataOf(state, first);
    const std::string second_data = trailDataOf(state, second);

    CHECK_FALSE(first_data.empty());
    CHECK_FALSE(second_data.empty());
    CHECK(first_data != second_data);
    CHECK(state.pack.findAddedAsset(first_data) != nullptr);
    CHECK(state.pack.findAddedAsset(second_data) != nullptr);
}

TEST_CASE("selecting the other trail opens the other trail's points", "[trail]") {
    editor::State state;
    startPack(state);

    const size_t first = pathable_edit::addTrail(state, kMapId);
    const size_t second = pathable_edit::addTrail(state, kMapId);
    REQUIRE(first != pack::kNoIndex);
    REQUIRE(second != pack::kNoIndex);

    // addTrail leaves the second one selected.
    frame(state);
    REQUIRE(trail_edit::isOpen());
    REQUIRE(trail_edit::pathable() == second);

    trail_edit::insertPoint(state, pack::kNoIndex, glm::vec3(1.0f, 2.0f, 3.0f));
    trail_edit::insertPoint(state, pack::kNoIndex, glm::vec3(4.0f, 5.0f, 6.0f));
    CHECK(trail_edit::pointCount() == 2);

    state.selection.set(editor::NodeRef{editor::NodeKind::Pathable, first});
    frame(state);

    CHECK(trail_edit::pathable() == first);
    CHECK(trail_edit::path() == pack::normalizePath(trailDataOf(state, first)));
    // The first trail was never given points; showing the second one's here is the bug
    // this case exists for.
    CHECK(trail_edit::pointCount() == 0);

    state.selection.set(editor::NodeRef{editor::NodeKind::Pathable, second});
    frame(state);

    CHECK(trail_edit::pathable() == second);
    CHECK(trail_edit::pointCount() == 2);
    CHECK(trail_edit::point(0) == glm::vec3(1.0f, 2.0f, 3.0f));

    trail_edit::close(state);
}

// A trail whose .trl cannot be read is the case that made the editor keep showing the
// previous trail: the read is cached as a miss, so it never succeeds on a later frame.
TEST_CASE("a trail that cannot be opened does not leave the last one showing", "[trail]") {
    editor::State state;
    startPack(state);

    const size_t good = pathable_edit::addTrail(state, kMapId);
    REQUIRE(good != pack::kNoIndex);
    frame(state);
    trail_edit::insertPoint(state, pack::kNoIndex, glm::vec3(1.0f, 2.0f, 3.0f));
    REQUIRE(trail_edit::pointCount() == 1);

    const size_t broken = pathable_edit::addTrail(state, kMapId);
    REQUIRE(broken != pack::kNoIndex);

    // Version 3 rather than 0: unreadable, the way a .trl from a future format would be.
    const std::string broken_path = trailDataOf(state, broken);
    std::vector<uint8_t> bytes(8, 0);
    bytes[0] = 3;
    state.pack.addAsset(broken_path, bytes);
    resources::forget(broken_path);

    state.selection.set(editor::NodeRef{editor::NodeKind::Pathable, broken});
    // Several frames: a read that is merely over budget must still be retried.
    for (int i = 0; i < 4; ++i) frame(state);

    CHECK_FALSE(trail_edit::isOpen());
    CHECK_FALSE(trail_edit::pathable() == good);
    CHECK(trail_edit::pointCount() == 0);

    trail_edit::close(state);
}

// The same thing over a pack that came off disk, which is where the report came from: a
// real pack already ships .trl files, so the name a new one gets has to dodge those too.
TEST_CASE("two trails in a real pack's category get their own .trl", "[trail]") {
    const std::filesystem::path folder = corpus::folderPack();
    if (folder.empty()) {
        SKIP("No folder pack: set MPE_TEST_FOLDER_PACK");
    }

    editor::State state;
    undo::clear();

    std::string error;
    std::unique_ptr<pack::DataSource> source = pack::open(folder, &error);
    REQUIRE(source);
    REQUIRE(pack::loadPack(*source, &state.pack, &error));
    resources::setSource(std::move(source));
    resources::setPendingAssets(&state.pack);
    resources::beginFrame();
    state.rebuildIndex();
    state.loaded = true;

    // A category that already holds a trail, so the new ones land beside real trail data.
    size_t host = pack::kNoIndex;
    for (const pack::Pathable& pathable : state.pack.pathables) {
        if (pathable.type == pack::PathableType::Trail && pathable.category != pack::kNoIndex) {
            host = pathable.category;
            break;
        }
    }
    if (host == pack::kNoIndex) SKIP("The folder pack has no trails");

    state.selection.set(editor::NodeRef{editor::NodeKind::Category, host});

    const size_t first = pathable_edit::addTrail(state, kMapId);
    const size_t second = pathable_edit::addTrail(state, kMapId);
    REQUIRE(first != pack::kNoIndex);
    REQUIRE(second != pack::kNoIndex);

    INFO("first  " << trailDataOf(state, first));
    INFO("second " << trailDataOf(state, second));
    CHECK(trailDataOf(state, first) != trailDataOf(state, second));

    resources::setSource(nullptr);
    resources::setPendingAssets(nullptr);
}

// The report's flow, end to end: lay out one trail, lay out a second, then go back to the
// first. Frames in between, because opening a trail is a budgeted read and the editor is
// meant to catch up on a later one.
TEST_CASE("laying out a second trail leaves the first one's points alone", "[trail]") {
    const std::filesystem::path folder = corpus::folderPack();
    if (folder.empty()) SKIP("No folder pack: set MPE_TEST_FOLDER_PACK");

    editor::State state;
    undo::clear();

    std::string error;
    std::unique_ptr<pack::DataSource> source = pack::open(folder, &error);
    REQUIRE(source);
    REQUIRE(pack::loadPack(*source, &state.pack, &error));
    resources::setSource(std::move(source));
    resources::setPendingAssets(&state.pack);
    state.rebuildIndex();
    state.loaded = true;

    size_t host = pack::kNoIndex;
    for (const pack::Pathable& pathable : state.pack.pathables) {
        if (pathable.type == pack::PathableType::Trail && pathable.category != pack::kNoIndex) {
            host = pathable.category;
            break;
        }
    }
    if (host == pack::kNoIndex) SKIP("The folder pack has no trails");
    state.selection.set(editor::NodeRef{editor::NodeKind::Category, host});

    const size_t first = pathable_edit::addTrail(state, kMapId);
    REQUIRE(first != pack::kNoIndex);
    for (int i = 0; i < 3; ++i) frame(state);
    REQUIRE(trail_edit::pathable() == first);
    trail_edit::insertPoint(state, pack::kNoIndex, glm::vec3(10.0f, 0.0f, 10.0f));
    trail_edit::insertPoint(state, pack::kNoIndex, glm::vec3(20.0f, 0.0f, 20.0f));
    REQUIRE(trail_edit::pointCount() == 2);

    const size_t second = pathable_edit::addTrail(state, kMapId);
    REQUIRE(second != pack::kNoIndex);
    for (int i = 0; i < 3; ++i) frame(state);
    INFO("first  " << trailDataOf(state, first));
    INFO("second " << trailDataOf(state, second));
    REQUIRE(trail_edit::pathable() == second);
    CHECK(trail_edit::pointCount() == 0);
    trail_edit::insertPoint(state, pack::kNoIndex, glm::vec3(30.0f, 0.0f, 30.0f));

    state.selection.set(editor::NodeRef{editor::NodeKind::Pathable, first});
    for (int i = 0; i < 3; ++i) frame(state);
    CHECK(trail_edit::pathable() == first);
    CHECK(trail_edit::pointCount() == 2);

    trail_edit::close(state);
    resources::setSource(nullptr);
    resources::setPendingAssets(nullptr);
}

// Renaming a trail's traildata is how a trail gets named — it is the only name the tree
// and the trail editor show — so the .trl has to move with it rather than being left
// behind under the old path.
TEST_CASE("renaming traildata takes the trail's points with it", "[trail]") {
    editor::State state;
    startPack(state);

    const size_t first = pathable_edit::addTrail(state, kMapId);
    REQUIRE(first != pack::kNoIndex);
    frame(state);
    REQUIRE(trail_edit::pathable() == first);
    trail_edit::insertPoint(state, pack::kNoIndex, glm::vec3(1.0f, 2.0f, 3.0f));
    trail_edit::insertPoint(state, pack::kNoIndex, glm::vec3(4.0f, 5.0f, 6.0f));
    REQUIRE(trail_edit::pointCount() == 2);

    const std::string old_path = trailDataOf(state, first);
    const editor::NodeRef node{editor::NodeKind::Pathable, first};
    const std::string renamed = "Data/route-one.trl";

    // What the property panel does: write the value, then hand the rename the value the
    // field started from.
    state.pack.pathables[first].attributes.set("traildata", renamed);
    pathable_edit::attributeRenamed(state, node, "traildata", old_path);

    CHECK(state.pack.findAddedAsset(renamed) != nullptr);
    // The trail it came from is the editor's own file and nothing else points at it.
    CHECK(state.pack.findAddedAsset(old_path) == nullptr);

    frame(state);
    CHECK(trail_edit::isOpen());
    CHECK(trail_edit::path() == pack::normalizePath(renamed));
    CHECK(trail_edit::pointCount() == 2);
    CHECK(trail_edit::point(1) == glm::vec3(4.0f, 5.0f, 6.0f));

    trail_edit::close(state);
}

// The other half of the rule: a traildata edit that names data the pack already has means
// "draw that instead", not "overwrite it with mine".
TEST_CASE("pointing a trail at existing data leaves that data alone", "[trail]") {
    editor::State state;
    startPack(state);

    const size_t first = pathable_edit::addTrail(state, kMapId);
    const size_t second = pathable_edit::addTrail(state, kMapId);
    REQUIRE(first != pack::kNoIndex);
    REQUIRE(second != pack::kNoIndex);

    frame(state);
    REQUIRE(trail_edit::pathable() == second);
    trail_edit::insertPoint(state, pack::kNoIndex, glm::vec3(7.0f, 8.0f, 9.0f));

    const std::string first_path = trailDataOf(state, first);
    const std::string second_path = trailDataOf(state, second);
    const std::vector<uint8_t> before = state.pack.findAddedAsset(first_path)->bytes;

    state.pack.pathables[second].attributes.set("traildata", first_path);
    pathable_edit::attributeRenamed(state, editor::NodeRef{editor::NodeKind::Pathable, second}, "traildata", second_path);

    CHECK(state.pack.findAddedAsset(first_path)->bytes == before);
    // The second trail's own file is still there: nothing was moved, so nothing was dropped.
    CHECK(state.pack.findAddedAsset(second_path) != nullptr);

    trail_edit::close(state);
}

// The rename and the file that moved with it are one edit, so one Ctrl+Z has to put both
// back — a trail restored to a path whose .trl went with the rename would be broken by its
// own undo.
TEST_CASE("undo takes a traildata rename and its .trl back together", "[trail]") {
    editor::State state;
    startPack(state);

    const size_t first = pathable_edit::addTrail(state, kMapId);
    REQUIRE(first != pack::kNoIndex);
    frame(state);
    trail_edit::insertPoint(state, pack::kNoIndex, glm::vec3(1.0f, 2.0f, 3.0f));

    const std::string old_path = trailDataOf(state, first);
    const editor::NodeRef node{editor::NodeKind::Pathable, first};
    const std::string renamed = "Data/route-one.trl";

    {
        undo::Step step(state, "Edit traildata");
        undo::attributes(state, node);
        state.pack.pathables[first].attributes.set("traildata", renamed);
        pathable_edit::attributeRenamed(state, node, "traildata", old_path);
    }

    REQUIRE(undo::canUndo());
    undo::undo(state);

    CHECK(trailDataOf(state, first) == old_path);
    CHECK(state.pack.findAddedAsset(old_path) != nullptr);
    CHECK(state.pack.findAddedAsset(renamed) == nullptr);

    frame(state);
    CHECK(trail_edit::isOpen());
    CHECK(trail_edit::pointCount() == 1);

    trail_edit::close(state);
}
