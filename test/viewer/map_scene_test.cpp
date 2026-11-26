// What the minimap and the full map show and where: Pathing's FlatMap and RenderToMiniMap,
// over a live map the test builds by hand, drawn into a draw list with no device.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <memory>
#include <vector>

#include "draw_list.h"
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

TEST_CASE("map scene: nothing is drawn on the Super Adventure Box's maps") {
    runtime::LiveMap live;
    live.markers.push_back(marker({500, 0, 500}));
    live.trails.push_back(trail({{400, 0, 500}, {600, 0, 500}}));
    REQUIRE(build(live).icons.size() == 1);

    for (const int sab : {935, 895, 934}) {
        MumbleState state = mumble();
        state.map_id = sab;
        const map_scene::MapScene scene = build(live, state);
        CHECK(scene.icons.empty());
        CHECK(scene.paths.empty());
    }
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

    map_scene::Options on;
    on.above_below = true;
    const map_scene::MapScene scene = build(live, mumble(), on);
    REQUIRE(scene.icons.size() == 4);
    for (const map_scene::Icon& icon : scene.icons) {
        const int expected[] = {1, -1, 0, 0};
        CHECK(icon.above_below == expected[icon.marker]);
    }

    // Off by default.
    for (const map_scene::Icon& icon : build(live).icons) CHECK(icon.above_below == 0);
}

TEST_CASE("map scene: an icon well above wears the game's above image, as opaque as the map whatever its own alpha") {
    runtime::LiveMap live;
    live.markers.push_back(marker({500, 31, 520}));
    live.markers.back().marker.alpha = 0.5f;
    map_scene::Options options;
    options.above_below = true;
    map_scene::MapScene scene = build(live, mumble(), options, 0.8f);
    int above = 0;
    int below = 0;
    scene.above = &above;
    scene.below = &below;

    test::DrawListFixture fixture;
    map_scene::draw(&fixture.list(), scene, *map::view(mumble(), kDisplay, 1.0f), 0.0f);
    const ImDrawList& list = fixture.list();
    bool drew_above = false;
    bool drew_below = false;
    for (const ImDrawCmd& cmd : list.CmdBuffer) {
        drew_above |= cmd.ElemCount > 0 && cmd.TextureId == &above;
        drew_below |= cmd.ElemCount > 0 && cmd.TextureId == &below;
    }
    CHECK(drew_above);
    CHECK_FALSE(drew_below);

    // The icon at its alpha under the map's, 0.5 × 0.8; the image, drawn last, at the map's alone.
    const auto alpha = [](const ImDrawVert& vertex) { return static_cast<int>((vertex.col >> IM_COL32_A_SHIFT) & 0xFF); };
    CHECK(alpha(list.VtxBuffer.front()) == Approx(102).margin(1));
    CHECK(alpha(list.VtxBuffer.back()) == Approx(204).margin(1));

    // Nothing to draw it with, nothing drawn.
    scene.above = nullptr;
    test::DrawListFixture without;
    map_scene::draw(&without.list(), scene, *map::view(mumble(), kDisplay, 1.0f), 0.0f);
    CHECK(without.list().VtxBuffer.Size == 4); // the icon's one quad
}

TEST_CASE("map scene: a trail is a line per stretch, faded by how far above or below it is") {
    runtime::LiveMap live;
    live.trails.push_back(trail({{100, 0, 100}, {200, 0, 300}, {300, 100, 300}, {400, 1000, 300}}));
    live.trails.back().trail.sample_colour = IM_COL32(0, 255, 0, 255);

    const map_scene::MapScene scene = build(live, mumble(), {}, 0.5f);
    REQUIRE(scene.paths.size() == 1);
    REQUIRE(scene.trails.size() == 1);
    const std::vector<map_scene::Line>& lines = scene.trails[0]->lines;
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
    CHECK(map_scene::lineOpacity(unfaded, unfaded.trails[0]->lines[2], 0.0f) == 1.0f);
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

TEST_CASE("map scene: every layer's icons are sorted together, and each keeps its own trails") {
    runtime::LiveMap installed;
    installed.markers = {marker({100, 0, 100}), marker({500, 0, 500})};
    installed.trails.push_back(trail({{100, 0, 100}, {200, 0, 300}}));
    runtime::LiveMap edited;
    edited.markers = {marker({300, 0, 300})};
    edited.trails.push_back(trail({{400, 0, 400}, {450, 0, 450}, {500, 0, 400}}));

    const std::vector<char> no_nodes;
    map_scene::TrailCache installed_trails;
    map_scene::TrailCache edited_trails;
    map_scene::MapScene scene;
    map_scene::build(
        {map_scene::Layer{&installed, &no_nodes, everyTexture(), &installed_trails},
         map_scene::Layer{&edited, &no_nodes, everyTexture(), &edited_trails, true}},
        squareKilometre(),
        mumble(),
        {},
        1.0f,
        1.0f,
        &scene);

    // The farthest from the camera, over the middle of the square, first.
    REQUIRE(scene.icons.size() == 3);
    CHECK((scene.icons[0].layer == 0 && scene.icons[0].marker == 0));
    CHECK((scene.icons[1].layer == 1 && scene.icons[1].marker == 0));
    CHECK((scene.icons[2].layer == 0 && scene.icons[2].marker == 1));

    REQUIRE(scene.trails.size() == 2);
    REQUIRE(scene.paths.size() == 2);
    CHECK(scene.paths[0].layer == 0);
    CHECK(scene.paths[1].layer == 1);
    CHECK(scene.trails[1]->lines.size() == 2);
    CHECK(roughly(scene.trails[1]->lines[scene.paths[1].first].from, glm::dvec2(400.0, 600.0)));
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

TEST_CASE("map scene: the tooltip says how far above or below the player the marker is") {
    runtime::LiveMarker live = marker({500, 10, 600}); // 10 m up, in MumbleLink's y
    live.marker.tip_name = "Chest";
    registry::CategoryTree tree;

    CHECK(map_scene::tooltip(live, tree, glm::vec3(500.0f, 500.0f, 0.0f), false).description == "3,957 away, 394 above");
    CHECK(map_scene::tooltip(live, tree, glm::vec3(500.0f, 500.0f, 20.0f), false).description == "3,957 away, 394 below");
    // heightOffset only lifts the icon in the world: it is measured from where it stands.
    live.marker.height_offset = 10.0f;
    live.marker.position.y += 10.0f;
    CHECK(map_scene::tooltip(live, tree, glm::vec3(500.0f, 500.0f, 0.0f), false).description == "3,957 away, 394 above");
}

TEST_CASE("map scene: the tooltip says which categories the marker sits in, each cut short") {
    registry::CategoryTree tree;
    tree.nodes.resize(4);
    tree.nodes[0].label = "Haskha's Markers";
    tree.nodes[1].label = "w8";
    tree.nodes[1].parent = 0;
    tree.nodes[2].label = "Chest";
    tree.nodes[2].parent = 1;
    tree.nodes[3].label = "H\xC3\xA9ro\xC3\xAFquesmentlong"; //"Héroïquesmentlong", in UTF-8
    tree.nodes[3].parent = 1;

    runtime::LiveMarker live = marker({500, 0, 600});
    live.marker.tip_name = "Chest";
    live.node = 2;
    const glm::vec3 player(500.0f, 500.0f, 0.0f);
    CHECK(map_scene::tooltip(live, tree, player, false).description == "Haskha's.../w8\n3,937 away");

    // A title that is not the category's name leaves the category in.
    live.marker.tip_name = "Big chest";
    CHECK(map_scene::tooltip(live, tree, player, false).description == "Haskha's.../w8/Chest\n3,937 away");

    // No space to cut at: cut mid-word, never inside a character.
    live.node = 3;
    CHECK(map_scene::tooltip(live, tree, player, false).description == "Haskha's.../w8/H\xC3\xA9ro\xC3\xAFques...\n3,937 away");

    // A marker of the root category, under its name, has no path.
    live.marker.tip_name = "Haskha's Markers";
    live.node = 0;
    CHECK(map_scene::tooltip(live, tree, player, false).description == "3,937 away");
}

TEST_CASE("map scene: the tooltip sits above and right of the cursor's tip, else left of it") {
    const glm::vec2 size(200.0f, 50.0f);
    const glm::vec2 display(1920.0f, 1080.0f);

    // Room above and right: its bottom left corner a gap off the tip.
    glm::vec2 at = map_scene::tooltipPosition(glm::vec2(500.0f, 500.0f), size, display);
    CHECK(at.x > 500.0f);
    CHECK(at.y + size.y < 500.0f);

    // Against the right edge: left of the tip, still above it.
    at = map_scene::tooltipPosition(glm::vec2(1800.0f, 500.0f), size, display);
    CHECK(at.x + size.x < 1800.0f);
    CHECK(at.y + size.y < 500.0f);

    // Against the top: left of the tip, kept on the screen, where the cursor never hangs.
    at = map_scene::tooltipPosition(glm::vec2(500.0f, 20.0f), size, display);
    CHECK(at.x + size.x < 500.0f);
    CHECK(at.y == 0.0f);

    // In the top left corner nothing fits beside the tip; it stays on the screen.
    at = map_scene::tooltipPosition(glm::vec2(10.0f, 10.0f), size, display);
    CHECK(at == glm::vec2(0.0f, 0.0f));
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

namespace {

// MumbleLink as `state` describes it, for what reads the link itself.
Mumble::Data mumbleData(const MumbleState& state) {
    Mumble::Data data{};
    data.UITick = 1;
    // MumbleLink's axes: y up.
    data.AvatarPosition.X = state.player_position.x;
    data.AvatarPosition.Y = state.player_position.z;
    data.AvatarPosition.Z = state.player_position.y;
    data.Context.Compass.Width = static_cast<unsigned short>(state.compass_width);
    data.Context.Compass.Height = static_cast<unsigned short>(state.compass_height);
    data.Context.Compass.Rotation = static_cast<float>(state.compass_rotation);
    data.Context.Compass.Scale = static_cast<float>(state.map_scale);
    data.Context.Compass.Center.X = static_cast<float>(state.map_center.x);
    data.Context.Compass.Center.Y = static_cast<float>(state.map_center.y);
    data.Context.IsMapOpen = state.is_map_open;
    data.Context.IsCompassTopRight = state.is_compass_top_right;
    data.Context.IsCompassRotating = state.is_compass_rotation_enabled;
    return data;
}

// The full map up, MumbleLink's compass fields now its own: the whole screen, centred elsewhere.
MumbleState fullMap() {
    MumbleState state = mumble();
    state.is_map_open = true;
    state.map_scale = 2.0;
    state.map_center = glm::dvec2(300.0, 300.0);
    return state;
}

struct Box {
    glm::vec2 min{std::numeric_limits<float>::max()};
    glm::vec2 max{std::numeric_limits<float>::lowest()};
    glm::vec2 middle() const { return (min + max) * 0.5f; }
};

Box drawnBox(const ImDrawList& list) {
    Box box;
    for (const ImDrawVert& vertex : list.VtxBuffer) {
        box.min = glm::min(box.min, glm::vec2(vertex.pos.x, vertex.pos.y));
        box.max = glm::max(box.max, glm::vec2(vertex.pos.x, vertex.pos.y));
    }
    return box;
}

} // namespace

TEST_CASE("map scene: under the full map, the minimap is drawn on the compass it had") {
    runtime::LiveMap live;
    live.markers.push_back(marker({500, 0, 500})); // the compass's middle: (1820, 100)
    const MumbleState on_compass = map_scene::withCompass(fullMap(), map_scene::compassOf(mumble()));
    auto minimap = std::make_shared<map_scene::MapScene>(build(live, on_compass));
    const Mumble::Data link = mumbleData(fullMap());

    SECTION("with the compass kept") {
        minimap->keep_compass = true;
        map_scene::publish(minimap, nullptr);
        test::DrawListFixture fixture;
        map_scene::drawPublished(&fixture.list(), &link, kDisplay, false);
        REQUIRE(fixture.list().VtxBuffer.Size > 0);
        const Box box = drawnBox(fixture.list());
        CHECK(box.min.x == Approx(1810.0f));
        CHECK(box.max.x == Approx(1830.0f));
        CHECK(box.min.y == Approx(90.0f));
        CHECK(box.max.y == Approx(110.0f));
    }

    SECTION("on the frame the map came up on, on the compass a scene built before it had") {
        // A moment can read MumbleLink after IsMapOpen came on and before a Present saw it: once
        // in a recording, November 2026, the minimap's markers dropped for that frame.
        map_scene::publish(minimap, nullptr);
        test::DrawListFixture fixture;
        map_scene::drawPublished(&fixture.list(), &link, kDisplay, false);
        REQUIRE(fixture.list().VtxBuffer.Size > 0);
        CHECK(drawnBox(fixture.list()).middle().x == Approx(1820.0f));
        CHECK(drawnBox(fixture.list()).middle().y == Approx(100.0f));
    }

    SECTION("with IsMapOpen off and the fields still the full map's, on the compass kept") {
        // The other way round, seen too: the minimap drawn at the full map's scale for a frame.
        MumbleState stale = fullMap();
        stale.is_map_open = false;
        const Mumble::Data stale_link = mumbleData(stale);
        minimap->keep_compass = true;
        map_scene::publish(minimap, nullptr);
        test::DrawListFixture fixture;
        map_scene::drawPublished(&fixture.list(), &stale_link, kDisplay, false);
        REQUIRE(fixture.list().VtxBuffer.Size > 0);
        CHECK(drawnBox(fixture.list()).middle().x == Approx(1820.0f));
        CHECK(drawnBox(fixture.list()).middle().y == Approx(100.0f));
    }

    SECTION("the full map, none published, draws nothing") {
        map_scene::publish(minimap, nullptr);
        test::DrawListFixture fixture;
        map_scene::drawPublished(&fixture.list(), &link, kDisplay, true);
        CHECK(fixture.list().VtxBuffer.Size == 0);
    }
    map_scene::publish(nullptr, nullptr);
}

TEST_CASE("map scene: the full map is drawn while it is up, zoomed as the game zooms it") {
    runtime::LiveMap live;
    live.markers.push_back(marker({500, 0, 500})); // continent (500, 500): 100 px right of and below the middle
    auto full_map = std::make_shared<map_scene::MapScene>(build(live, fullMap()));
    map_scene::publish(nullptr, full_map);
    const Mumble::Data up = mumbleData(fullMap());

    test::DrawListFixture still;
    map_scene::drawPublished(&still.list(), &up, kDisplay, true);
    REQUIRE(still.list().VtxBuffer.Size > 0);
    const Box still_box = drawnBox(still.list());

    SECTION("still: where MumbleLink's view puts it") {
        CHECK(still_box.middle().x == Approx(1060.0f));
        CHECK(still_box.middle().y == Approx(640.0f));
    }

    SECTION("zoomed: the picture scaled about the screen's middle, the icon with it") {
        full_map->zoom = 3.0f;
        test::DrawListFixture fixture;
        map_scene::drawPublished(&fixture.list(), &up, kDisplay, true);
        const Box box = drawnBox(fixture.list());
        CHECK(box.middle().x == Approx(1260.0f));
        CHECK(box.middle().y == Approx(840.0f));
        CHECK(box.max.x - box.min.x == Approx((still_box.max.x - still_box.min.x) * 3.0f));
    }

    SECTION("not once MumbleLink has the map down") {
        const Mumble::Data down = mumbleData(mumble());
        test::DrawListFixture fixture;
        map_scene::drawPublished(&fixture.list(), &down, kDisplay, true);
        CHECK(fixture.list().VtxBuffer.Size == 0);
    }
    map_scene::publish(nullptr, nullptr);
}

TEST_CASE("map scene: the full map zooms down to 1 at 39.5 times a second, landing 0.54 s after it opens") {
    map_scene::FullMapZoom zoom;
    CHECK(zoom.update(false, true, 10.0).zoom == 1.0f);
    CHECK(zoom.update(true, true, 20.0).zoom == Approx(1.0 + 39.5 * 0.54));
    CHECK(zoom.update(true, true, 20.44).zoom == Approx(1.0 + 39.5 * 0.1));
    CHECK(zoom.update(true, true, 20.54).zoom == Approx(1.0f));
    const map_scene::FullMapZoom::Frame still = zoom.update(true, false, 21.0);
    CHECK(still.zoom == 1.0f);
    CHECK(still.opacity == 1.0f);
    // Opening again starts over.
    zoom.update(false, true, 30.0);
    CHECK(zoom.update(true, true, 31.0).zoom == Approx(1.0 + 39.5 * 0.54));
}

TEST_CASE("map scene: the full map zooms back and fades from the first frame the world is drawn again") {
    map_scene::FullMapZoom zoom;
    zoom.update(true, true, 0.0);
    zoom.update(true, true, 0.6);
    // Fully up: the game draws no world, so no moment comes.
    for (int frame = 1; frame <= 5; ++frame) CHECK(zoom.update(true, false, 0.6 + frame * 0.016).zoom == 1.0f);

    SECTION("the world back is the closing, the game's picture 40 ms behind it") {
        CHECK(zoom.update(true, true, 2.0).zoom == Approx(1.0f));
        const map_scene::FullMapZoom::Frame waiting = zoom.update(true, true, 2.03);
        CHECK(waiting.zoom == Approx(1.0f));
        CHECK(waiting.opacity == Approx(1.0f));
        const map_scene::FullMapZoom::Frame closing = zoom.update(true, true, 2.14);
        CHECK(closing.zoom == Approx(1.0 + 39.5 * 0.1));
        CHECK(closing.opacity == Approx(0.75f));
        CHECK(zoom.update(true, true, 2.5).opacity == 0.0f);
        CHECK(zoom.update(true, true, 3.0).zoom == Approx(1.0 + 39.5 * 0.54));
        // Down at last: still again, for the next time.
        CHECK(zoom.update(false, true, 3.1).zoom == 1.0f);
    }

    SECTION("a world missing during the opening says nothing") {
        map_scene::FullMapZoom opening;
        opening.update(true, true, 0.0);
        opening.update(true, false, 0.1);
        opening.update(true, false, 0.2);
        opening.update(true, false, 0.3);
        CHECK(opening.update(true, true, 0.6).zoom == 1.0f);
    }

    SECTION("nor a frame or two GoodRender missed") {
        map_scene::FullMapZoom missed;
        missed.update(true, true, 0.0);
        missed.update(true, true, 0.6);
        missed.update(true, false, 0.62);
        missed.update(true, false, 0.63);
        CHECK(missed.update(true, true, 0.64).zoom == 1.0f);
        CHECK(missed.update(true, true, 0.8).zoom == 1.0f);
    }
}

TEST_CASE("map scene: without GoodRender the closing is not seen, and the map stays still") {
    map_scene::FullMapZoom zoom;
    zoom.update(true, false, 0.0);
    for (int frame = 34; frame <= 100; ++frame) {
        const map_scene::FullMapZoom::Frame now = zoom.update(true, false, frame * 0.016);
        CHECK(now.zoom == 1.0f);
        CHECK(now.opacity == 1.0f);
    }
}

TEST_CASE("map scene: the compass is kept from the full map coming up until MumbleLink gives its fields back") {
    map_scene::CompassKeeper keeper;
    const MumbleState compass = mumble();
    CHECK_FALSE(keeper.update(compass).keep);

    map_scene::CompassKeeper::Kept kept = keeper.update(fullMap());
    CHECK(kept.keep);
    CHECK(kept.compass.scale == compass.map_scale);
    CHECK(kept.compass.centre == compass.map_center);

    // IsMapOpen off, the fields still the full map's: kept.
    MumbleState stale = fullMap();
    stale.is_map_open = false;
    kept = keeper.update(stale);
    CHECK(kept.keep);
    CHECK(kept.compass.scale == compass.map_scale);

    // The compass's own fields back, moved since: they are the compass now.
    MumbleState back = compass;
    back.map_center = glm::dvec2(510.0, 500.0);
    kept = keeper.update(back);
    CHECK_FALSE(kept.keep);
    CHECK(kept.compass.centre == back.map_center);
}

TEST_CASE("map scene: an icon's image is kept once there, as the world's is") {
    runtime::LiveMap live;
    live.markers = {marker({500.0f, 0.0f, 500.0f})};
    size_t asked = 0;
    const TextureLookup counted = [&asked](size_t, const std::string&) {
        ++asked;
        return test::fakeTexture();
    };

    map_scene::MapScene scene;
    map_scene::TrailCache trails;
    for (int frame = 0; frame < 3; ++frame) {
        map_scene::build(live, {}, squareKilometre(), mumble(), {}, 1.0f, 1.0f, counted, &trails, &scene);
        CHECK(scene.icons.size() == 1);
    }
    CHECK(asked == 1);
    CHECK(live.markers[0].image == test::fakeTexture());
}
