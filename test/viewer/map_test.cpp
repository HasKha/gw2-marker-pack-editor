// Where the world lands on the minimap and the full map: the maps' rectangles as the API and
// Pathing's fallback file give them, the projection, and the compass's bounds on screen.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <string>

#include "corpus.h"
#include "http/http.h"
#include "viewer/map.h"
#include "viewer/mumble.h"

using namespace viewer::map;
using Catch::Approx;

namespace {

// Queensdale as /v2/maps/15 gives it, trimmed to what is read.
constexpr const char* kQueensdaleApi = R"([{
    "id": 15, "name": "Queensdale",
    "map_rect": [[-43008, -27648], [43008, 30720]],
    "continent_rect": [[42624, 28032], [46208, 30464]]
}])";

// Queensdale as Pathing's ref/fallback/mapDetails.json has it.
constexpr const char* kQueensdaleFallback = R"({"15":{"ContinentRectTopLeftX":42624.0,"ContinentRectTopLeftY":28032.0,
    "MapRectTopLeftX":-43008.0,"MapRectTopLeftY":30720.0,"MapRectWidth":86016.0,"MapRectHeight":58368.0,
    "ContinentRectWidth":3584.0,"ContinentRectHeight":2432.0}})";

void checkSame(const MapDetails& a, const MapDetails& b) {
    CHECK(a.continent_top_left == b.continent_top_left);
    CHECK(a.continent_size == b.continent_size);
    CHECK(a.map_top_left == b.map_top_left);
    CHECK(a.map_size == b.map_size);
}

viewer::MumbleState compass(int width, int height) {
    viewer::MumbleState mumble;
    mumble.available = true;
    mumble.compass_width = width;
    mumble.compass_height = height;
    mumble.map_scale = 2.0;
    mumble.map_center = glm::dvec2(1000.0, 2000.0);
    return mumble;
}

constexpr glm::vec2 kDisplay{1920.0f, 1080.0f};

} // namespace

TEST_CASE("maps: the API's rectangles read as Pathing's fallback file has them") {
    const std::optional<MapTable> api = mapsFromApi(kQueensdaleApi);
    const std::optional<MapTable> fallback = mapsFromCache(kQueensdaleFallback);
    REQUIRE(api);
    REQUIRE(fallback);
    REQUIRE(api->count(15) == 1);
    REQUIRE(fallback->count(15) == 1);
    checkSame(api->at(15), fallback->at(15));
}

TEST_CASE("maps: the cache reads back what it wrote") {
    const MapTable maps = *mapsFromApi(kQueensdaleApi);
    const std::optional<MapTable> again = mapsFromCache(mapsToCache(maps));
    REQUIRE(again);
    REQUIRE(again->size() == 1);
    checkSame(again->at(15), maps.at(15));
}

TEST_CASE("maps: what is not a map is skipped, and what is not a list is nothing") {
    CHECK_FALSE(mapsFromApi("{}"));
    CHECK_FALSE(mapsFromApi("not json"));
    CHECK_FALSE(mapsFromCache("[]"));

    const std::optional<MapTable> partial = mapsFromApi(R"([{"id": 1}, {"id": 2, "map_rect": [[0, 0], [1, 1]],
        "continent_rect": [[0, 0], [1, 1]]}, {"id": "3"}])");
    REQUIRE(partial);
    CHECK(partial->size() == 1);
    CHECK(partial->count(2) == 1);

    const std::optional<MapTable> cached = mapsFromCache(R"({"x": {}, "4": {"MapRectWidth": 1}})");
    REQUIRE(cached);
    CHECK(cached->empty());
}

TEST_CASE("maps: the world's corners land on the continent's") {
    const MapDetails queensdale = mapsFromApi(kQueensdaleApi)->at(15);
    constexpr float kInches = 39.37f;

    // The map's top left, in metres: x at its least, z at its greatest.
    const glm::dvec2 top_left = continentPosition(queensdale, glm::vec3(-43008.0f / kInches, 50.0f, 30720.0f / kInches));
    CHECK(top_left.x == Approx(42624.0));
    CHECK(top_left.y == Approx(28032.0));

    const glm::dvec2 bottom_right = continentPosition(queensdale, glm::vec3(43008.0f / kInches, 0.0f, -27648.0f / kInches));
    CHECK(bottom_right.x == Approx(46208.0));
    CHECK(bottom_right.y == Approx(30464.0));

    // North is up the continent: a greater z is a smaller y.
    const glm::dvec2 origin = continentPosition(queensdale, glm::vec3(0.0f));
    const glm::dvec2 north = continentPosition(queensdale, glm::vec3(0.0f, 0.0f, 100.0f));
    CHECK(north.y < origin.y);
    CHECK(north.x == Approx(origin.x));
}

TEST_CASE("maps: the compass sits in its corner, sized in UI pixels") {
    viewer::MumbleState mumble = compass(300, 250);

    SECTION("bottom right, over the experience bar") {
        const std::optional<View> bottom = view(mumble, kDisplay, 1.0f);
        REQUIRE(bottom);
        CHECK_FALSE(bottom->full_map);
        CHECK(bottom->max.x == 1920.0f);
        CHECK(bottom->min.x == 1620.0f);
        CHECK(bottom->max.y == Approx(1080.0f - 40.0f * 0.897f));
        CHECK(bottom->max.y - bottom->min.y == Approx(250.0f));
    }

    SECTION("top right") {
        mumble.is_compass_top_right = true;
        const std::optional<View> top = view(mumble, kDisplay, 1.0f);
        REQUIRE(top);
        CHECK(top->min == glm::vec2(1620.0f, 0.0f));
        CHECK(top->max == glm::vec2(1920.0f, 250.0f));
    }

    SECTION("the interface size scales it") {
        mumble.is_compass_top_right = true;
        const std::optional<View> larger = view(mumble, kDisplay, uiScale(3));
        REQUIRE(larger);
        CHECK(larger->max.x - larger->min.x == Approx(300.0f * 1.22f));
        CHECK(larger->pixels_per_unit == Approx(1.22 / 2.0));
    }

    SECTION("the full map covers the screen") {
        mumble.is_map_open = true;
        const std::optional<View> full = view(mumble, kDisplay, 1.0f);
        REQUIRE(full);
        CHECK(full->full_map);
        CHECK(full->min == glm::vec2(0.0f));
        CHECK(full->max == kDisplay);
    }

    SECTION("nothing before the game has sized the compass") {
        CHECK_FALSE(view(compass(0, 250), kDisplay, 1.0f));
        CHECK_FALSE(view(viewer::MumbleState{}, kDisplay, 1.0f));
    }
}

TEST_CASE("maps: the projection scales by MapScale and turns with the compass") {
    viewer::MumbleState mumble = compass(200, 200);
    mumble.is_compass_top_right = true; // bounds 1720..1920 x 0..200, middle (1820, 100)

    const View still = *view(mumble, kDisplay, 1.0f);
    CHECK(project(still, mumble.map_center) == glm::vec2(1820.0f, 100.0f));
    // Two continent units to the UI pixel.
    const glm::vec2 east = project(still, mumble.map_center + glm::dvec2(20.0, 0.0));
    CHECK(east.x == Approx(1830.0f));
    CHECK(east.y == Approx(100.0f));

    mumble.is_compass_rotation_enabled = true;
    mumble.compass_rotation = std::numbers::pi / 2.0;
    const View turned = *view(mumble, kDisplay, 1.0f);
    const glm::vec2 turned_east = project(turned, mumble.map_center + glm::dvec2(20.0, 0.0));
    CHECK(turned_east.x == Approx(1820.0f).margin(1e-3));
    CHECK(turned_east.y == Approx(110.0f));

    // The full map does not turn, whatever the compass does.
    mumble.is_map_open = true;
    const View full = *view(mumble, kDisplay, 1.0f);
    CHECK_FALSE(full.rotate);
    CHECK(project(full, mumble.map_center + glm::dvec2(20.0, 0.0)).x == Approx(970.0f));
}

TEST_CASE("maps: the interface sizes' scales, with Normal as one") {
    CHECK(uiScale(0) == 0.90f);
    CHECK(uiScale(1) == 1.0f);
    CHECK(uiScale(2) == 1.11f);
    CHECK(uiScale(3) == 1.22f);
    CHECK(uiScale(9) == 1.0f);
}

TEST_CASE("maps: the catalog keeps the cache until the API answers, then caches the answer") {
    const corpus::TempDir temp("map_catalog");
    const std::filesystem::path file = temp.path() / "maps.json";
    {
        std::ofstream(file) << kQueensdaleFallback;
    }

    SECTION("no answer: the cache stands") {
        Catalog catalog;
        catalog.start(file, [](const http::Cancel&) { return std::optional<std::string>(); });
        catalog.stop();
        REQUIRE(catalog.find(15));
        CHECK(catalog.find(15)->continent_size == glm::dvec2(3584.0, 2432.0));
        CHECK(catalog.find(16) == nullptr);
    }

    SECTION("an answer replaces it, and is written for the next session") {
        Catalog catalog;
        catalog.start(file, [](const http::Cancel&) {
            return std::optional<std::string>(R"([{"id": 16, "map_rect": [[0, 0], [10, 10]], "continent_rect": [[0, 0], [1, 1]]}])");
        });
        catalog.stop();
        CHECK(catalog.find(15) == nullptr);
        REQUIRE(catalog.find(16));

        Catalog next;
        next.start(file, [](const http::Cancel&) { return std::optional<std::string>(); });
        next.stop();
        CHECK(next.find(16));
    }

    SECTION("no cache yet") {
        Catalog catalog;
        catalog.start(temp.path() / "missing.json", [](const http::Cancel&) { return std::optional<std::string>(); });
        catalog.stop();
        CHECK(catalog.find(15) == nullptr);
    }
}
