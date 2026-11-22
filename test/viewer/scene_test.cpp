// From installed packs to what a sink submits: the category toggles, the live map, the scene,
// and the GoodRender sink run against a moment of the test's own. No device anywhere — what
// would be drawn is what is asserted.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <cmath>

#include <vector>

#include "corpus.h"
#include "viewer/render/good_render.h"
#include "viewer/runtime.h"
#include "viewer/scene.h"
#include "viewer/store.h"
#include "viewer/test_camera.h"

using Catch::Approx;

namespace {

constexpr int kMap = 15;

viewer::BakedCategory category(std::string id, size_t parent = pack::kNoIndex) {
    viewer::BakedCategory baked;
    baked.name = id.substr(id.rfind('.') + 1);
    baked.id = std::move(id);
    baked.parent = parent;
    return baked;
}

viewer::BakedMarker marker(glm::vec3 position, size_t category, int map = kMap) {
    viewer::BakedMarker baked;
    baked.position = position;
    baked.category = category;
    baked.map_id = map;
    baked.icon = "icon.png";
    return baked;
}

// A pack with categories "a" and "a.b", and whatever pathables the test gives it.
std::unique_ptr<viewer::registry::LoadedPack> loadedPack(std::string path, viewer::BakedPathables pathables) {
    auto loaded = std::make_unique<viewer::registry::LoadedPack>();
    loaded->path = std::move(path);
    loaded->categories = {category("a"), category("a.b", 0)};
    loaded->pathables = std::move(pathables);
    return loaded;
}

// Any non-null id will do: nothing samples it.
ImTextureID fakeTexture() {
    static int texture = 0;
    return &texture;
}

viewer::TextureLookup everyTexture() {
    return [](size_t, const std::string& path) { return path.empty() ? nullptr : fakeTexture(); };
}

struct Built {
    viewer::runtime::Installed installed;
    viewer::store::CategoryStates states;
    viewer::Scene scene;

    void build(const viewer::Camera& camera, const viewer::TextureLookup& textures = everyTexture(), float seconds = 0.0f) {
        const viewer::runtime::LiveMap live = viewer::runtime::liveMap(installed, kMap);
        viewer::buildScene(installed, live, viewer::runtime::visibleNodes(installed.tree, states), camera, seconds, textures, &scene);
    }
};

// What a sink handed to world_quad, kept by a moment's function pointer.
std::vector<std::array<glm::vec3, 4>> submitted;

void recordQuad(const float corners[4][3], const float[4][2], void*, uint32_t) {
    submitted.push_back({glm::make_vec3(corners[0]), glm::make_vec3(corners[1]), glm::make_vec3(corners[2]), glm::make_vec3(corners[3])});
}

good_render::Camera momentCamera(const viewer::Camera& camera) {
    good_render::Camera out{};
    for (int i = 0; i < 3; ++i) {
        out.position[i] = camera.position[i];
        out.front[i] = camera.front[i];
        out.up[i] = camera.up[i];
    }
    out.fov = camera.fov;
    out.aspect = camera.aspect;
    std::copy_n(glm::value_ptr(camera.view), 16, out.view);
    std::copy_n(glm::value_ptr(camera.projection), 16, out.projection);
    std::copy_n(glm::value_ptr(camera.view_projection), 16, out.view_projection);
    return out;
}

} // namespace

TEST_CASE("a choice equal to the pack's default is forgotten", "[store]") {
    viewer::store::CategoryStates states;
    CHECK(states.enabled("a", true));

    const uint64_t before = states.revision();
    states.set("a", false, true);
    CHECK_FALSE(states.enabled("a", true));
    CHECK(states.revision() != before);

    // Back to the default: nothing is kept, so a pack that later changes its default is followed.
    states.set("a", true, true);
    CHECK(states.enabled("a", false) == false);
}

TEST_CASE("category choices survive a save and a load", "[store]") {
    const corpus::TempDir temp("store_categories");
    viewer::store::CategoryStates states;
    states.set("a.b", false, true);
    states.set("c", true, false);
    REQUIRE(states.save(temp / viewer::store::CATEGORIES_FILE_NAME));

    viewer::store::CategoryStates loaded;
    loaded.load(temp / viewer::store::CATEGORIES_FILE_NAME);
    CHECK_FALSE(loaded.enabled("a.b", true));
    CHECK(loaded.enabled("c", false));
    CHECK(loaded.enabled("untouched", true));
}

TEST_CASE("a category shows only when every ancestor is on", "[runtime]") {
    viewer::registry::CategoryTree tree;
    viewer::registry::mergeCategories(&tree, {category("a"), category("a.b", 0), category("a.b.c", 1)});

    viewer::store::CategoryStates states;
    states.set("a", false, true);
    const std::vector<char> visible = viewer::runtime::visibleNodes(tree, states);
    CHECK_FALSE(visible[tree.find("a.b.c")]);

    states.set("a", true, true);
    CHECK(viewer::runtime::visibleNodes(tree, states)[tree.find("a.b.c")]);
}

TEST_CASE("the live map holds the current map's pathables from every pack", "[runtime]") {
    viewer::runtime::Installed installed;
    viewer::BakedPathables first;
    first.markers = {marker({0, 0, 5}, 0), marker({0, 0, 5}, 0, kMap + 1)};
    viewer::BakedPathables second;
    second.markers = {marker({0, 0, 5}, 1)};
    viewer::runtime::install(&installed, loadedPack("first", first));
    viewer::runtime::install(&installed, loadedPack("second", second));

    const viewer::runtime::LiveMap live = viewer::runtime::liveMap(installed, kMap);
    REQUIRE(live.markers.size() == 2);
    CHECK(live.markers[0].pack == 0);
    CHECK(live.markers[1].pack == 1);

    // Both packs declare "a": one node between them.
    CHECK(installed.tree.nodes.size() == 2);
    CHECK(installed.packs[0]->node_of == installed.packs[1]->node_of);
}

TEST_CASE("taking a pack away merges the tree again from the others", "[runtime]") {
    viewer::runtime::Installed installed;
    viewer::runtime::install(&installed, loadedPack("first", {}));
    auto second = loadedPack("second", {});
    second->categories.push_back(category("z"));
    viewer::runtime::install(&installed, std::move(second));
    REQUIRE(installed.tree.find("z") != pack::kNoIndex);

    const uint64_t before = installed.revision;
    CHECK(viewer::runtime::uninstall(&installed, "second"));
    CHECK(installed.packs.size() == 1);
    CHECK(installed.tree.find("z") == pack::kNoIndex);
    CHECK(installed.revision != before);
    CHECK_FALSE(viewer::runtime::uninstall(&installed, "second"));
}

TEST_CASE("the scene leaves out what is off, faded out or has no icon", "[scene]") {
    Built built;
    viewer::BakedPathables pathables;
    pathables.markers = {marker({0, 0, 5}, 0), marker({0, 0, 6}, 1), marker({0, 0, 7}, 1)};
    pathables.markers[2].icon.clear();
    pathables.markers.push_back(marker({0, 0, 50}, 0));
    pathables.markers[3].fade_near = 100.0f; // inches; 50 m is far past both
    pathables.markers[3].fade_far = 200.0f;
    viewer::runtime::install(&built.installed, loadedPack("pack", pathables));

    const viewer::Camera camera = test_camera::axisCamera();
    built.build(camera);
    REQUIRE(built.scene.items.size() == 2);

    built.states.set("a.b", false, true);
    built.build(camera);
    REQUIRE(built.scene.items.size() == 1);
    CHECK(built.scene.items[0].centre.z == Approx(5.0f));
}

TEST_CASE("the scene is back to front, billboards and quads together", "[scene]") {
    Built built;
    viewer::BakedPathables pathables;
    pathables.markers = {marker({0, 0, 5}, 0), marker({0, 0, 30}, 0), marker({0, 0, 10}, 0)};
    pathables.markers[2].has_rotation = true;
    viewer::runtime::install(&built.installed, loadedPack("pack", pathables));

    built.build(test_camera::axisCamera());
    REQUIRE(built.scene.items.size() == 3);
    CHECK(built.scene.items[0].centre.z == Approx(30.0f));
    CHECK(built.scene.items[1].kind == viewer::SceneItem::Kind::Quad);
    CHECK(built.scene.items[2].centre.z == Approx(5.0f));
}

TEST_CASE("nothing is in the scene while the map is open", "[scene]") {
    Built built;
    viewer::BakedPathables pathables;
    pathables.markers = {marker({0, 0, 5}, 0)};
    viewer::runtime::install(&built.installed, loadedPack("pack", pathables));

    viewer::Camera camera = test_camera::axisCamera();
    camera.map_open = true;
    built.build(camera);
    CHECK(built.scene.items.empty());
}

TEST_CASE("a trail is in the scene as its ribbon's quads", "[scene]") {
    Built built;
    viewer::BakedPathables pathables;
    viewer::BakedTrail trail;
    trail.category = 0;
    trail.map_id = kMap;
    trail.texture = "trail.png";
    auto geometry = std::make_shared<pack::trl::Trail>();
    geometry->segments.push_back({{{0, -1, 2}, {0, -1, 3}, {0, -1, 4}}, 0});
    trail.geometry = geometry;
    pathables.trails = {trail};
    viewer::runtime::install(&built.installed, loadedPack("pack", pathables));

    built.build(test_camera::axisCamera());
    REQUIRE(built.scene.items.size() == 2);
    for (const viewer::SceneItem& item : built.scene.items) CHECK(item.kind == viewer::SceneItem::Kind::Quad);
}

TEST_CASE("a moment's camera projects as ours does", "[good_render]") {
    const viewer::Camera ours = test_camera::axisCamera();
    const viewer::Camera theirs = viewer::good_render_sink::cameraFromMoment(momentCamera(ours), ours.viewport);

    glm::vec2 a(0.0f), b(0.0f);
    REQUIRE(ours.project({3, 2, 20}, &a));
    REQUIRE(theirs.project({3, 2, 20}, &b));
    CHECK(a.x == Approx(b.x));
    CHECK(a.y == Approx(b.y));
}

TEST_CASE("the GoodRender sink submits what the overlay would draw", "[good_render]") {
    const viewer::Camera camera = test_camera::axisCamera();

    viewer::Scene scene;
    viewer::SceneItem billboard;
    billboard.kind = viewer::SceneItem::Kind::Billboard;
    billboard.centre = {0, 0, 40};
    billboard.size = 2.0f;
    billboard.min_size = 0.0f;
    billboard.max_size = 1e6f;
    scene.items.push_back(billboard);

    // So small and far that minSize has to scale it up.
    viewer::SceneItem tiny = billboard;
    tiny.centre = {0, 0, 900};
    tiny.size = 0.01f;
    tiny.min_size = 20.0f;
    scene.items.push_back(tiny);

    viewer::SceneItem quad;
    quad.corners[0] = {0, 0, 1};
    quad.corners[1] = {1, 0, 1};
    quad.corners[2] = {1, 1, 1};
    quad.corners[3] = {0, 1, 1};
    scene.items.push_back(quad);

    const good_render::Camera moment_camera = momentCamera(camera);
    good_render::Moment moment{};
    moment.moment = good_render::MOMENT_WORLD_FINISHED;
    moment.width = static_cast<uint32_t>(test_camera::kWidth);
    moment.height = static_cast<uint32_t>(test_camera::kHeight);
    moment.pixels_per_window_pixel = 1.0f;
    moment.camera = &moment_camera;
    moment.world_quad = recordQuad;

    submitted.clear();
    viewer::good_render_sink::submit(moment, scene);
    REQUIRE(submitted.size() == 3);

    glm::vec3 expected[4];
    REQUIRE(camera.billboardQuad(billboard.centre, billboard.size, expected));
    for (int i = 0; i < 4; ++i) CHECK(glm::length(submitted[0][i] - expected[i]) == Approx(0.0f).margin(1e-5f));

    // minSize 20 is 40 real pixels tall (see clampedBillboardSize), wherever the marker is.
    glm::vec2 top(0.0f), bottom(0.0f);
    REQUIRE(camera.project(submitted[1][0], &bottom));
    REQUIRE(camera.project(submitted[1][3], &top));
    CHECK(std::abs(bottom.y - top.y) == Approx(40.0f).epsilon(0.01));

    CHECK(submitted[2][1] == quad.corners[1]);
}

TEST_CASE("a real pack draws on its own map, and only there", "[scene][corpus]") {
    const std::filesystem::path path = corpus::pack("haskha_markers.zip");
    if (path.empty()) SKIP("haskha_markers.zip is not in the corpus");

    Built built;
    std::unique_ptr<viewer::registry::LoadedPack> loaded = std::make_unique<viewer::registry::LoadedPack>(viewer::registry::loadPack(path));
    REQUIRE(loaded->error.empty());
    REQUIRE_FALSE(loaded->pathables.markers.empty());
    const viewer::BakedMarker first = loaded->pathables.markers.front();
    viewer::runtime::install(&built.installed, std::move(loaded));

    // Standing at the first marker, looking at it from two metres back.
    viewer::Camera camera = test_camera::axisCamera();
    camera.player = first.position;
    camera.position = first.position - glm::vec3(0.0f, 0.0f, 2.0f);
    camera.view = glm::lookAtLH(camera.position, camera.position + camera.front, camera.up);
    camera.view_projection = camera.projection * camera.view;

    const viewer::runtime::LiveMap live = viewer::runtime::liveMap(built.installed, first.map_id);
    REQUIRE_FALSE(live.markers.empty());
    viewer::buildScene(
        built.installed,
        live,
        viewer::runtime::visibleNodes(built.installed.tree, built.states),
        camera,
        1.5f,
        everyTexture(),
        &built.scene);
    CHECK_FALSE(built.scene.items.empty());
    for (const viewer::SceneItem& item : built.scene.items) {
        for (const glm::vec3& corner : item.corners) REQUIRE(std::isfinite(corner.x + corner.y + corner.z));
        REQUIRE(std::isfinite(item.centre.x + item.centre.y + item.centre.z));
    }

    const viewer::runtime::LiveMap elsewhere = viewer::runtime::liveMap(built.installed, -12345);
    CHECK(elsewhere.markers.empty());
    CHECK(elsewhere.trails.empty());
}
