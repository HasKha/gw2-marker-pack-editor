// The pack open in the editor, drawn from its model: the edited pack's live map, and the scene
// layer that carries its pathables to the editor's overlay.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <map>
#include <memory>
#include <string>

#include "corpus.h"
#include "pack/source.h"
#include "pack/xml_reader.h"
#include "viewer/draw_list.h"
#include "viewer/edited.h"
#include "viewer/scene.h"
#include "viewer/test_camera.h"

using Catch::Approx;

namespace {

constexpr int kMap = 15;
constexpr int kTrailMap = 50;

// Two markers on kMap, one on another map, and a trail whose file puts it on kTrailMap.
pack::Pack editedPack(const corpus::TempDir& temp) {
    std::ofstream(temp / "markers.xml") << "<OverlayData>\n"
                                           "<MarkerCategory name=\"c\" DisplayName=\"C\"/>\n"
                                           "<POIs>\n"
                                           "<POI type=\"c\" MapID=\"15\" xpos=\"0\" ypos=\"0\" zpos=\"5\" iconFile=\"a.png\"/>\n"
                                           "<POI type=\"c\" MapID=\"15\" xpos=\"0\" ypos=\"0\" zpos=\"6\" iconFile=\"b.png\"/>\n"
                                           "<POI type=\"c\" MapID=\"16\" xpos=\"0\" ypos=\"0\" zpos=\"7\"/>\n"
                                           "<Trail type=\"c\" trailData=\"t.trl\" texture=\"t.png\"/>\n"
                                           "</POIs>\n"
                                           "</OverlayData>\n";
    std::string error;
    const std::unique_ptr<pack::DataSource> source = pack::open(temp.path(), &error);
    REQUIRE(source != nullptr);
    pack::Pack model;
    REQUIRE(pack::loadPack(*source, &model, &error));
    REQUIRE(model.pathables.size() == 4);
    return model;
}

// The editor's trail reads: `t.trl` on kTrailMap, deferred `deferrals` times first.
struct FakeTrails {
    int deferrals = 0;
    int reads = 0;

    viewer::edited::TrailLookup lookup() {
        return [this](const std::string& path, bool* deferred) -> std::shared_ptr<const pack::trl::Trail> {
            *deferred = false;
            if (path != "t.trl") return nullptr;
            if (deferrals > 0) {
                --deferrals;
                *deferred = true;
                return nullptr;
            }
            ++reads;
            auto trail = std::make_shared<pack::trl::Trail>();
            trail->map_id = kTrailMap;
            trail->segments.push_back({{{0, -1, 2}, {0, -1, 3}, {0, -1, 4}}, 0});
            return trail;
        };
    }
};

float zOf(const viewer::runtime::LiveMarker& marker) {
    return marker.marker.position.z;
}

} // namespace

TEST_CASE("the edited pack's live map holds its markers on the current map", "[edited]") {
    const corpus::TempDir temp("edited_map");
    const pack::Pack model = editedPack(temp);
    FakeTrails trails;

    viewer::edited::EditedLive edited;
    viewer::edited::Report report{temp.path(), &model, 1, nullptr};
    edited.update(report, kMap, trails.lookup());
    REQUIRE(edited.live().markers.size() == 2);
    CHECK(edited.live().markers[0].marker.pathable == 0);
    CHECK(edited.live().markers[1].marker.pathable == 1);
    CHECK(edited.live().markers[0].node == pack::kNoIndex);
    CHECK(edited.live().trails.empty());

    edited.update(report, 16, trails.lookup());
    REQUIRE(edited.live().markers.size() == 1);
    CHECK(edited.live().markers[0].marker.pathable == 2);

    // The trail's map is its file's.
    edited.update(report, kTrailMap, trails.lookup());
    REQUIRE(edited.live().trails.size() == 1);
    CHECK(edited.live().trails[0].trail.geometry != nullptr);
}

TEST_CASE("an edit is baked again when the revision moves, and not before", "[edited]") {
    const corpus::TempDir temp("edited_revision");
    pack::Pack model = editedPack(temp);
    FakeTrails trails;

    viewer::edited::EditedLive edited;
    CHECK(edited.update({temp.path(), &model, 1, nullptr}, kMap, trails.lookup()));
    REQUIRE(edited.live().markers.size() == 2);
    CHECK(zOf(edited.live().markers[0]) == Approx(5.0f));

    model.pathables[0].attributes.set("zpos", "9");
    CHECK_FALSE(edited.update({temp.path(), &model, 1, nullptr}, kMap, trails.lookup()));
    CHECK(zOf(edited.live().markers[0]) == Approx(5.0f));

    CHECK(edited.update({temp.path(), &model, 2, nullptr}, kMap, trails.lookup()));
    CHECK(zOf(edited.live().markers[0]) == Approx(9.0f));

    // Each build is a revision of its own, which the map's trail lines go by.
    const uint64_t built = edited.live().revision;
    CHECK(edited.update({temp.path(), &model, 3, nullptr}, kMap, trails.lookup()));
    CHECK(edited.live().revision > built);

    // A behavior's state stays between frames while nothing changed.
    edited.live().markers[0].behavior.filtered = true;
    CHECK_FALSE(edited.update({temp.path(), &model, 3, nullptr}, kMap, trails.lookup()));
    CHECK(edited.live().markers[0].behavior.filtered);
}

TEST_CASE("what the editor's tree hides is not in the edited live map", "[edited]") {
    const corpus::TempDir temp("edited_shown");
    const pack::Pack model = editedPack(temp);
    FakeTrails trails;

    viewer::edited::EditedLive edited;
    std::vector<char> shown{1, 0, 1, 1};
    edited.update({temp.path(), &model, 1, &shown}, kMap, trails.lookup());
    REQUIRE(edited.live().markers.size() == 1);
    CHECK(edited.live().markers[0].marker.pathable == 0);

    // A view state: it moves no revision, and still shows at once.
    shown[1] = 1;
    edited.update({temp.path(), &model, 1, &shown}, kMap, trails.lookup());
    CHECK(edited.live().markers.size() == 2);
}

TEST_CASE("a trail whose read was put off arrives on a later frame", "[edited]") {
    const corpus::TempDir temp("edited_deferred");
    const pack::Pack model = editedPack(temp);
    FakeTrails trails;
    trails.deferrals = 2;

    viewer::edited::EditedLive edited;
    const viewer::edited::Report report{temp.path(), &model, 1, nullptr};
    edited.update(report, kTrailMap, trails.lookup());
    CHECK(edited.live().trails.empty());
    edited.update(report, kTrailMap, trails.lookup());
    CHECK(edited.live().trails.empty());
    edited.update(report, kTrailMap, trails.lookup());
    CHECK(edited.live().trails.size() == 1);

    // Once it is in, a frame with nothing new reads nothing.
    edited.update(report, kTrailMap, trails.lookup());
    CHECK(trails.reads == 1);
}

TEST_CASE("no pack open leaves the edited live map empty", "[edited]") {
    const corpus::TempDir temp("edited_closed");
    const pack::Pack model = editedPack(temp);
    FakeTrails trails;

    viewer::edited::EditedLive edited;
    edited.update({temp.path(), &model, 1, nullptr}, kMap, trails.lookup());
    REQUIRE_FALSE(edited.live().markers.empty());
    CHECK(edited.update({}, kMap, trails.lookup()));
    CHECK(edited.live().markers.empty());
    CHECK(edited.baked().markers.empty());
    CHECK_FALSE(edited.update({}, kMap, trails.lookup()));
}

TEST_CASE("the edited layer names its pathables and keeps what has no image", "[edited][scene]") {
    const corpus::TempDir temp("edited_scene");
    const pack::Pack model = editedPack(temp);
    FakeTrails trails;

    viewer::edited::EditedLive edited;
    edited.update({temp.path(), &model, 1, nullptr}, kMap, trails.lookup());

    viewer::runtime::LiveMap installed_live;
    installed_live.map_id = kMap;
    viewer::BakedMarker other;
    other.position = {0, 0, 8};
    other.icon = "other.png";
    other.pathable = 0;
    installed_live.markers.push_back({0, pack::kNoIndex, other, 0});

    // b.png is missing from the edited pack.
    const viewer::TextureLookup textures = [](size_t, const std::string& path) {
        return path == "b.png" ? nullptr : viewer::test::fakeTexture();
    };
    const std::vector<char> no_nodes;
    viewer::Scene scene;
    viewer::buildScene(
        {viewer::SceneLayer{&installed_live, &no_nodes, textures, false}, viewer::SceneLayer{&edited.live(), &no_nodes, textures, true}},
        test_camera::axisCamera(),
        0.0f,
        {},
        &scene);

    REQUIRE(scene.items.size() == 3);
    std::map<float, const viewer::SceneItem*> by_depth;
    for (const viewer::SceneItem& item : scene.items) by_depth[item.centre.z] = &item;
    CHECK(by_depth.at(8.0f)->pathable == pack::kNoIndex);
    CHECK(by_depth.at(5.0f)->pathable == 0);
    CHECK(by_depth.at(5.0f)->texture != nullptr);
    CHECK(by_depth.at(6.0f)->pathable == 1);
    CHECK(by_depth.at(6.0f)->texture == nullptr);
    // Every layer sorted together, back to front.
    CHECK(scene.items.front().centre.z == Approx(8.0f));
    CHECK(scene.items.back().centre.z == Approx(5.0f));
}

TEST_CASE("an edited trail's quads say they are trail", "[edited][scene]") {
    const corpus::TempDir temp("edited_trail_scene");
    const pack::Pack model = editedPack(temp);
    FakeTrails trails;

    viewer::edited::EditedLive edited;
    edited.update({temp.path(), &model, 1, nullptr}, kTrailMap, trails.lookup());
    REQUIRE(edited.live().trails.size() == 1);

    viewer::Camera camera = test_camera::axisCamera();
    camera.map_id = kTrailMap;
    const std::vector<char> no_nodes;
    const viewer::TextureLookup textures = [](size_t, const std::string&) { return viewer::test::fakeTexture(); };
    viewer::Scene scene;
    viewer::buildScene({viewer::SceneLayer{&edited.live(), &no_nodes, textures, true}}, camera, 0.0f, {}, &scene);
    REQUIRE_FALSE(scene.items.empty());
    for (const viewer::SceneItem& item : scene.items) {
        CHECK(item.is_trail);
        CHECK(item.pathable == 3);
    }
}
