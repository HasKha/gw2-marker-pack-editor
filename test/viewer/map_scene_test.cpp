// What the minimap and the full map show and where: Pathing's FlatMap and RenderToMiniMap,
// over a live map the test builds by hand, drawn into a draw list with no device.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "viewer/draw_list.h"
#include "viewer/map.h"
#include "viewer/map_scene.h"
#include "viewer/runtime.h"

using namespace viewer;
using Catch::Approx;

namespace {

// A kilometre square whose continent coordinates are its metres: (x, z) lands on (x, 1000 - z).
map::MapDetails squareKilometre() {
    map::MapDetails details;
    details.map_top_left = glm::dvec2(0.0, 39370.0);
    details.map_size = glm::dvec2(39370.0, 39370.0);
    details.continent_top_left = glm::dvec2(0.0, 0.0);
    details.continent_size = glm::dvec2(1000.0, 1000.0);
    return details;
}

runtime::LiveMarker marker(glm::vec3 position) {
    runtime::LiveMarker live;
    live.marker.position = position;
    live.marker.icon = "icon.png";
    return live;
}

runtime::LiveTrail trail(std::vector<glm::vec3> points) {
    runtime::LiveTrail live;
    auto geometry = std::make_shared<pack::trl::Trail>();
    geometry->segments.push_back(pack::trl::Segment{std::move(points)});
    live.trail.geometry = std::move(geometry);
    return live;
}

TextureLookup everyTexture() {
    return [](size_t, const std::string& path) { return path.empty() ? nullptr : test::fakeTexture(); };
}

// The player in the middle of the square, on the ground; the minimap up, top right.
MumbleState mumble() {
    MumbleState state;
    state.available = true;
    state.compass_width = 200;
    state.compass_height = 200;
    state.is_compass_top_right = true;
    state.map_scale = 1.0;
    state.map_center = glm::dvec2(500.0, 500.0);
    state.player_position = glm::vec3(500.0f, 500.0f, 0.0f); // Blish HUD's axes: z up
    state.camera_position = glm::vec3(500.0f, 500.0f, 10.0f);
    return state;
}

constexpr glm::vec2 kDisplay{1920.0f, 1080.0f};

// Pathing's 39.37 inches to the metre is a float, so metres come back a hair off.
bool roughly(const glm::dvec2& a, const glm::dvec2& b) {
    return a.x == Approx(b.x) && a.y == Approx(b.y);
}

map_scene::MapScene
build(const runtime::LiveMap& live, const MumbleState& state = mumble(), map_scene::Options options = {}, float opacity = 1.0f) {
    map_scene::MapScene scene;
    map_scene::TrailCache trails;
    map_scene::build(live, {}, squareKilometre(), state, options, opacity, 1.0f, everyTexture(), &trails, &scene);
    return scene;
}

} // namespace

TEST_CASE("map scene: each map's visibility setting, over each pathable's own") {
    runtime::LiveMap live;
    live.markers.push_back(marker({500, 0, 500}));
    live.markers.back().marker.minimap_visibility = false;
    live.markers.push_back(marker({510, 0, 500}));
    live.markers.back().marker.map_visibility = false;
    live.trails.push_back(trail({{500, 0, 500}, {510, 0, 500}}));
    live.trails.back().trail.minimap_visibility = false;

    MumbleState state = mumble();
    map_scene::Options options;

    SECTION("Default: each its own") {
        const map_scene::MapScene minimap = build(live, state, options);
        REQUIRE(minimap.icons.size() == 1);
        CHECK(minimap.icons[0].marker == 1);
        CHECK(minimap.paths.empty());

        state.is_map_open = true;
        const map_scene::MapScene full = build(live, state, options);
        REQUIRE(full.icons.size() == 1);
        CHECK(full.icons[0].marker == 0);
        CHECK(full.paths.size() == 1);
    }

    SECTION("Always and Never override them") {
        options.minimap_markers = map_scene::Visibility::Always;
        options.minimap_trails = map_scene::Visibility::Always;
        const map_scene::MapScene always = build(live, state, options);
        CHECK(always.icons.size() == 2);
        CHECK(always.paths.size() == 1);

        options.minimap_markers = map_scene::Visibility::Never;
        options.minimap_trails = map_scene::Visibility::Never;
        const map_scene::MapScene never = build(live, state, options);
        CHECK(never.icons.empty());
        CHECK(never.paths.empty());
    }

    SECTION("off shows nothing on either map") {
        options.enabled = false;
        CHECK(build(live, state, options).icons.empty());
        state.is_map_open = true;
        CHECK(build(live, state, options).icons.empty());
    }
}

TEST_CASE("map scene: what is off, gone, filtered or without its icon is not on the map") {
    runtime::LiveMap live;
    live.markers.push_back(marker({500, 0, 500}));
    live.markers.push_back(marker({500, 0, 500}));
    live.markers.back().removed = true;
    live.markers.push_back(marker({500, 0, 500}));
    live.markers.back().behavior.filtered = true;
    live.markers.push_back(marker({500, 0, 500}));
    live.markers.back().marker.icon.clear();
    live.markers.push_back(marker({500, 0, 500}));
    live.markers.back().node = 0;

    const map::MapDetails details = squareKilometre();
    map_scene::TrailCache trails;
    map_scene::MapScene scene;
    map_scene::build(live, {0}, details, mumble(), {}, 1.0f, 1.0f, everyTexture(), &trails, &scene);
    REQUIRE(scene.icons.size() == 1);
    CHECK(scene.icons[0].marker == 0);

    map_scene::Options ghosts;
    ghosts.show_hidden = true;
    map_scene::build(live, {0}, details, mumble(), ghosts, 1.0f, 1.0f, everyTexture(), &trails, &scene);
    CHECK(scene.icons.size() == 2);
}

TEST_CASE("map scene: an icon's colour is its tint, its alpha, the map's opacity and the fade") {
    runtime::LiveMap live;
    live.markers.push_back(marker({500, 0, 500}));
    live.markers.back().marker.tint = IM_COL32(255, 0, 0, 255);
    live.markers.back().marker.alpha = 0.5f;

    map_scene::Options options;
    options.minimap_opacity = 0.5f;
    const map_scene::MapScene scene = build(live, mumble(), options, 0.5f);
    REQUIRE(scene.icons.size() == 1);
    CHECK((scene.icons[0].colour & ~IM_COL32_A_MASK) == (IM_COL32(255, 0, 0, 0)));
    CHECK(((scene.icons[0].colour >> IM_COL32_A_SHIFT) & 0xFF) == 32); // 255 × 0.125, rounded

    options.minimap_opacity = 0.0f;
    CHECK(build(live, mumble(), options).icons.empty());
}

TEST_CASE("map scene: icons are drawn farthest from the camera first") {
    runtime::LiveMap live;
    live.markers.push_back(marker({501, 0, 500}));
    live.markers.push_back(marker({900, 0, 500}));
    live.markers.push_back(marker({600, 0, 500}));
    const map_scene::MapScene scene = build(live);
    REQUIRE(scene.icons.size() == 3);
    CHECK(scene.icons[0].marker == 1);
    CHECK(scene.icons[1].marker == 2);
    CHECK(scene.icons[2].marker == 0);
}

TEST_CASE("map scene: an icon says when it is more than 30 m above or below the player") {
    runtime::LiveMap live;
    live.markers.push_back(marker({500, 31, 500}));
    live.markers.push_back(marker({500, -31, 500}));
    live.markers.push_back(marker({500, 29, 500}));
    // heightOffset is not part of where the marker is.
    live.markers.push_back(marker({500, 40, 500}));
    live.markers.back().marker.height_offset = 20.0f;

    const map_scene::MapScene scene = build(live);
    REQUIRE(scene.icons.size() == 4);
    for (const map_scene::Icon& icon : scene.icons) {
        const int expected[] = {1, -1, 0, 0};
        CHECK(icon.above_below == expected[icon.marker]);
    }

    map_scene::Options off;
    off.above_below = false;
    for (const map_scene::Icon& icon : build(live, mumble(), off).icons) CHECK(icon.above_below == 0);
}

TEST_CASE("map scene: a trail is a line per stretch, faded by how far above or below it is") {
    runtime::LiveMap live;
    live.trails.push_back(trail({{100, 0, 100}, {200, 0, 300}, {300, 100, 300}, {400, 1000, 300}}));
    live.trails.back().trail.sample_colour = IM_COL32(0, 255, 0, 255);

    const map_scene::MapScene scene = build(live, mumble(), {}, 0.5f);
    REQUIRE(scene.paths.size() == 1);
    REQUIRE(scene.trails);
    const std::vector<map_scene::Line>& lines = scene.trails->lines;
    REQUIRE(lines.size() == 3);
    CHECK(scene.paths[0].colour == IM_COL32(0, 255, 0, 255));
    CHECK(roughly(scene.paths[0].min, glm::dvec2(100.0, 700.0)));
    CHECK(roughly(scene.paths[0].max, glm::dvec2(400.0, 900.0)));
    CHECK(roughly(lines[0].from, glm::dvec2(100.0, 900.0)));

    // The player on the ground; the fade-in at half.
    CHECK(map_scene::lineOpacity(scene, lines[0], 0.0f) == Approx(0.5f));
    CHECK(map_scene::lineOpacity(scene, lines[1], 0.0f) == Approx(0.375f)); // 50 m away: 0.75
    CHECK(map_scene::lineOpacity(scene, lines[2], 0.0f) == Approx(0.075f)); // 550 m away: at the least, 0.15
    CHECK(map_scene::lineOpacity(scene, lines[2], 550.0f) == Approx(0.5f));

    map_scene::Options flat;
    flat.fade_far_trails = false;
    const map_scene::MapScene unfaded = build(live, mumble(), flat);
    CHECK(map_scene::lineOpacity(unfaded, unfaded.trails->lines[2], 0.0f) == 1.0f);
}

TEST_CASE("map scene: a trail's lines are laid once per live map and kept") {
    runtime::LiveMap live;
    live.map_id = 15;
    live.revision = 3;
    live.trails.push_back(trail({{100, 0, 100}, {200, 0, 300}}));
    live.trails.push_back(trail({{300, 0, 300}}));
    const map::MapDetails details = squareKilometre();

    map_scene::TrailCache cache;
    const std::shared_ptr<const map_scene::TrailLines> first = cache.get(live, details);
    REQUIRE(first->paths.size() == 2);
    CHECK(first->paths[0].count == 1);
    CHECK(first->paths[1].count == 0); // one point is no line
    CHECK(cache.get(live, details) == first);

    // A script removing a trail does not change them: the frame skips it.
    live.trails[0].removed = true;
    CHECK(cache.get(live, details) == first);

    // Another set of packs, or the API's rectangles arriving, does.
    live.revision = 4;
    const std::shared_ptr<const map_scene::TrailLines> second = cache.get(live, details);
    CHECK(second != first);
    const map::MapDetails fresh = squareKilometre();
    CHECK(cache.get(live, fresh) != second);

    const map_scene::MapScene scene = build(live);
    CHECK(scene.paths.empty());
}

TEST_CASE("map scene: an icon's size follows the zoom unless it keeps its own") {
    map_scene::Icon icon;
    icon.continent = glm::dvec2(500.0, 500.0);
    icon.size = 20.0f;

    MumbleState state = mumble();
    glm::vec2 min, max;
    const map::View zoomed_in = *map::view(state, kDisplay, 1.0f);
    REQUIRE(map_scene::iconRect(icon, zoomed_in, &min, &max));
    CHECK(max.x - min.x == Approx(20.0f));
    CHECK((min + max) * 0.5f == glm::vec2(1820.0f, 100.0f));

    state.map_scale = 2.0;
    const map::View zoomed_out = *map::view(state, kDisplay, 1.0f);
    REQUIRE(map_scene::iconRect(icon, zoomed_out, &min, &max));
    CHECK(max.x - min.x == Approx(10.0f));

    icon.scale_with_zoom = false;
    REQUIRE(map_scene::iconRect(icon, zoomed_out, &min, &max));
    CHECK(max.x - min.x == Approx(20.0f * 0.897f));

    // Its centre off the compass: Pathing draws none of it.
    icon.continent = glm::dvec2(500.0, 1000.0);
    CHECK_FALSE(map_scene::iconRect(icon, zoomed_in, &min, &max));
}

TEST_CASE("map scene: what is drawn stays inside the compass") {
    runtime::LiveMap live;
    live.markers.push_back(marker({500, 0, 500}));
    live.markers.push_back(marker({595, 0, 500})); // its centre 5 px inside the edge, half of it out
    live.markers.push_back(marker({700, 0, 500})); // off the compass
    live.trails.push_back(trail({{400, 0, 500}, {900, 0, 500}}));
    live.trails.push_back(trail({{900, 0, 900}, {950, 0, 950}})); // off the compass whole

    const MumbleState state = mumble();
    const map_scene::MapScene scene = build(live, state);
    const map::View view = *map::view(state, kDisplay, 1.0f);

    test::DrawListFixture fixture;
    map_scene::draw(&fixture.list(), scene, view, 0.0f);
    ImDrawList& list = fixture.list();
    REQUIRE(list.VtxBuffer.Size > 0);
    for (const ImDrawVert& vertex : list.VtxBuffer) {
        // A line's thickness may reach past the bounds by its half width.
        CHECK(vertex.pos.x >= view.min.x - 2.0f);
        CHECK(vertex.pos.x <= view.max.x + 2.0f);
        CHECK(vertex.pos.y >= view.min.y - 2.0f);
        CHECK(vertex.pos.y <= view.max.y + 2.0f);
    }

    // The icon half out keeps the half of its image that is in.
    bool found_cut = false;
    for (const ImDrawVert& vertex : list.VtxBuffer) {
        if (vertex.pos.x == Approx(view.max.x) && vertex.uv.x == Approx(0.75f)) found_cut = true;
    }
    CHECK(found_cut);
}

TEST_CASE("map scene: the mouse is over the nearest icon drawn under it") {
    runtime::LiveMap live;
    live.markers.push_back(marker({500, 0, 500}));
    live.markers.push_back(marker({505, 0, 500}));
    live.markers.push_back(marker({900, 0, 500}));
    const MumbleState state = mumble();
    const map_scene::MapScene scene = build(live, state);
    const map::View view = *map::view(state, kDisplay, 1.0f);

    // Both of the first two cover (1823, 100); the one nearer the camera is drawn on top.
    const map_scene::Icon* over = map_scene::hovered(scene, view, glm::vec2(1823.0f, 100.0f));
    REQUIRE(over);
    CHECK(over->marker == 0);
    CHECK(map_scene::hovered(scene, view, glm::vec2(1700.0f, 100.0f)) == nullptr);
}

TEST_CASE("map scene: the tooltip is the tip, its description and how far away") {
    runtime::LiveMarker live = marker({500, 0, 600});
    live.marker.tip_name = "Chest";
    live.marker.tip_description = "Under the bridge";
    registry::CategoryTree tree;

    const glm::vec3 player(500.0f, 500.0f, 0.0f);
    map_scene::Tooltip tip = map_scene::tooltip(live, tree, player, false);
    CHECK(tip.title == "Chest");
    CHECK(tip.description == "Under the bridge\n\n3,937 away");

    live.marker.tip_description = "  ";
    CHECK(map_scene::tooltip(live, tree, player, false).description == "3,937 away");
    CHECK(map_scene::tooltip(live, tree, glm::vec3(500.0f, 600.0f, 0.0f), false).description == " away");

    live.marker.tip_name.clear();
    CHECK(map_scene::tooltip(live, tree, player, false).title.empty());
}

TEST_CASE("map scene: with Shift, the tooltip is the category path") {
    registry::CategoryTree tree;
    tree.nodes.resize(2);
    tree.nodes[0].label = "Tyria";
    tree.nodes[1].label = "Chests";
    tree.nodes[1].parent = 0;

    runtime::LiveMarker live = marker({500, 0, 500});
    live.node = 1;
    CHECK(map_scene::tooltip(live, tree, glm::vec3(0.0f), true).title == "Tyria\n > Chests");
    live.node = pack::kNoIndex;
    CHECK(map_scene::tooltip(live, tree, glm::vec3(0.0f), true).title == "Unassigned Category");
}

TEST_CASE("map scene: the map fades in after it opens, closes or the interface resizes") {
    map_scene::FadeIn fade;
    MumbleState state = mumble();
    CHECK(fade.update(state, 10.0) == 0.0f);
    CHECK(fade.update(state, 10.325) == Approx(0.5f));
    CHECK(fade.update(state, 11.0) == 1.0f);
    state.is_map_open = true;
    CHECK(fade.update(state, 12.0) == 0.0f);
    CHECK(fade.update(state, 13.0) == 1.0f);
    state.ui_size = 3;
    CHECK(fade.update(state, 14.0) == 0.0f);
}
