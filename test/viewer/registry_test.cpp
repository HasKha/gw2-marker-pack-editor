// The registry: which files are packs, loading one whole, merging categories across packs,
// and the worker that does the loading.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "corpus.h"
#include "viewer/registry.h"

namespace {

using corpus::writeFile;

viewer::BakedCategory category(std::string id, size_t parent = pack::kNoIndex) {
    viewer::BakedCategory baked;
    baked.name = id.substr(id.rfind('.') + 1);
    baked.id = std::move(id);
    baked.parent = parent;
    return baked;
}

} // namespace

TEST_CASE("every archive under a folder is a pack, and the folder is one when it has its own xml", "[registry]") {
    const corpus::TempDir temp("registry_discover");
    writeFile(temp / "b.zip", "");
    writeFile(temp / "nested" / "a.TACO", "");
    writeFile(temp / "notes.txt", "");

    std::vector<std::filesystem::path> found = viewer::registry::discoverPacks(temp.path());
    REQUIRE(found.size() == 2);
    CHECK(found[0] == temp / "b.zip");
    CHECK(found[1] == temp / "nested" / "a.TACO");

    // Pathing loads the folder itself as an unpacked pack too, last.
    writeFile(temp / "loose.xml", "<OverlayData/>");
    found = viewer::registry::discoverPacks(temp.path());
    REQUIRE(found.size() == 3);
    CHECK(found[2] == temp.path());
}

TEST_CASE("a pack was written when its archive was, or a folder's newest top-level xml", "[registry]") {
    using namespace std::chrono_literals;
    const corpus::TempDir temp("registry_written");
    const std::filesystem::path archive = temp / "pack.zip";
    writeFile(archive, "zip");
    const std::filesystem::file_time_type then = std::filesystem::last_write_time(archive);
    CHECK(viewer::registry::writtenAt(archive) == then);
    std::filesystem::last_write_time(archive, then + 1h);
    CHECK(viewer::registry::writtenAt(archive) == then + 1h);

    const std::filesystem::path folder = temp / "folder";
    std::filesystem::create_directories(folder / "data");
    writeFile(folder / "a.xml", "<OverlayData/>");
    writeFile(folder / "B.XML", "<OverlayData/>");
    writeFile(folder / "icon.png", "png");
    writeFile(folder / "data" / "deep.xml", "<OverlayData/>");
    std::filesystem::last_write_time(folder / "a.xml", then);
    std::filesystem::last_write_time(folder / "B.XML", then + 2h);
    std::filesystem::last_write_time(folder / "icon.png", then + 5h);
    std::filesystem::last_write_time(folder / "data" / "deep.xml", then + 5h);
    CHECK(viewer::registry::writtenAt(folder) == then + 2h);

    CHECK(viewer::registry::writtenAt(temp / "missing.zip") == std::filesystem::file_time_type{});
}

TEST_CASE("a folder that is not there holds no packs", "[registry]") {
    CHECK(viewer::registry::discoverPacks("Z:/no/such/folder").empty());
}

TEST_CASE("a pack that cannot be read says why and holds nothing", "[registry]") {
    const viewer::registry::LoadedPack loaded = viewer::registry::loadPack("Z:/no/such/pack.zip");
    CHECK_FALSE(loaded.error.empty());
    CHECK(loaded.source == nullptr);
    CHECK(loaded.pathables.markers.empty());
}

TEST_CASE("a loaded pack has every trail's map from its .trl", "[registry][corpus]") {
    const std::filesystem::path path = corpus::pack("haskha_markers.zip");
    if (path.empty()) SKIP("haskha_markers.zip is not in the corpus");

    const viewer::registry::LoadedPack loaded = viewer::registry::loadPack(path);
    REQUIRE(loaded.error.empty());
    REQUIRE(loaded.source != nullptr);
    CHECK_FALSE(loaded.pathables.markers.empty());
    REQUIRE_FALSE(loaded.pathables.trails.empty());
    CHECK_FALSE(loaded.categories.empty());

    for (const viewer::BakedTrail& trail : loaded.pathables.trails) {
        INFO(trail.data);
        REQUIRE(trail.geometry != nullptr);
        CHECK(trail.map_id == trail.geometry->map_id);
    }
}

TEST_CASE("packs sharing a category share its node", "[registry]") {
    viewer::registry::CategoryTree tree;

    std::vector<viewer::BakedCategory> first = {category("a"), category("a.b", 0)};
    first[0].display_name = "First";
    first[1].default_toggle = false;
    const std::vector<size_t> first_nodes = viewer::registry::mergeCategories(&tree, first);

    std::vector<viewer::BakedCategory> second = {category("a"), category("a.b", 0), category("a.c", 0), category("z")};
    second[0].display_name = "Second";
    const std::vector<size_t> second_nodes = viewer::registry::mergeCategories(&tree, second);

    REQUIRE(tree.nodes.size() == 4);
    CHECK(second_nodes[0] == first_nodes[0]);
    CHECK(second_nodes[1] == first_nodes[1]);

    // A later name wins; a flag the later pack says nothing about keeps what the first said.
    CHECK(tree.nodes[first_nodes[0]].label == "Second");
    CHECK_FALSE(tree.nodes[first_nodes[1]].default_toggle);

    // Children in the order packs first declared them, roots likewise.
    REQUIRE(tree.nodes[first_nodes[0]].children.size() == 2);
    CHECK(tree.nodes[tree.nodes[first_nodes[0]].children[1]].id == "a.c");
    REQUIRE(tree.roots.size() == 2);
    CHECK(tree.nodes[tree.roots[1]].id == "z");
    CHECK(tree.find("a.c") == second_nodes[2]);
    CHECK(tree.find("nope") == pack::kNoIndex);
}

TEST_CASE("a category with no display name is labelled by its name", "[registry]") {
    viewer::registry::CategoryTree tree;
    std::vector<viewer::BakedCategory> baked = {category("root"), category("root.Leaf", 0)};
    baked[1].display_name = "";
    viewer::registry::mergeCategories(&tree, baked);
    CHECK(tree.nodes[1].label == "Leaf");
}

TEST_CASE("the loader hands over every pack it was given", "[registry]") {
    const corpus::TempDir temp("registry_loader");
    writeFile(
        temp / "one" / "a.xml",
        R"(<OverlayData><MarkerCategory name="a"/><POIs><POI type="a" xpos="1" ypos="2" zpos="3"/></POIs></OverlayData>)");
    writeFile(temp / "two" / "b.xml", R"(<OverlayData><MarkerCategory name="b"/></OverlayData>)");

    viewer::registry::Loader loader;
    loader.add({temp / "one", temp / "two"});
    loader.add({temp / "missing"});

    std::vector<std::unique_ptr<viewer::registry::LoadedPack>> taken;
    for (int waited_ms = 0; loader.busy() && waited_ms < 5000; waited_ms += 5) {
        while (std::unique_ptr<viewer::registry::LoadedPack> next = loader.take()) taken.push_back(std::move(next));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(taken.size() == 3);
    CHECK(taken[0]->path == temp / "one");
    CHECK(taken[0]->pathables.markers.size() == 1);
    CHECK(taken[1]->categories.size() == 1);
    CHECK_FALSE(taken[2]->error.empty());
    CHECK_FALSE(loader.busy());
}

TEST_CASE("a stopped loader drops what it had and takes more", "[registry]") {
    const corpus::TempDir temp("registry_loader_stop");
    writeFile(temp / "one" / "a.xml", R"(<OverlayData><MarkerCategory name="a"/></OverlayData>)");

    viewer::registry::Loader loader;
    loader.add({temp / "one", temp / "one", temp / "one"});
    loader.stop();
    CHECK_FALSE(loader.busy());
    CHECK(loader.take() == nullptr);

    loader.add({temp / "one"});
    std::unique_ptr<viewer::registry::LoadedPack> taken;
    for (int waited_ms = 0; !taken && waited_ms < 5000; waited_ms += 5) {
        taken = loader.take();
        if (!taken) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(taken != nullptr);
    CHECK(taken->error.empty());
}

TEST_CASE("a pack that throws while it is read is handed over with the reason", "[registry]") {
    // A file name the ANSI code page has no characters for, which the folder source's listing
    // throws on: a worker must hand the pack over, not end the process.
    const corpus::TempDir temp("registry_loader_throws");
    writeFile(temp / "pack" / "a.xml", R"(<OverlayData><MarkerCategory name="a"/></OverlayData>)");
    writeFile(temp / "pack" / std::filesystem::path(u8"日本Ő.png"), "");

    viewer::registry::Loader loader;
    loader.add({temp / "pack"});
    std::unique_ptr<viewer::registry::LoadedPack> taken;
    for (int waited_ms = 0; !taken && waited_ms < 5000; waited_ms += 5) {
        taken = loader.take();
        if (!taken) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(taken != nullptr);
    CHECK(taken->path == temp / "pack");
    CHECK(taken->written == viewer::registry::writtenAt(temp / "pack"));
}

TEST_CASE("inside a folder is by path, not by name", "[registry]") {
    CHECK(viewer::registry::isInside("C:/addons/packs/Hero.zip", "C:/addons/packs"));
    CHECK(viewer::registry::isInside("C:/addons/packs/sub/../Hero.zip", "C:/addons/packs/"));
    CHECK_FALSE(viewer::registry::isInside("C:/addons/packs2/Hero.zip", "C:/addons/packs"));
    CHECK_FALSE(viewer::registry::isInside("C:/addons/Hero.zip", "C:/addons/packs"));
    CHECK_FALSE(viewer::registry::isInside("C:/addons/Hero.zip", ""));
}
