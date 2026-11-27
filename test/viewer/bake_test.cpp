// The bake: a pack resolved for drawing, one small pack per rule. That every pack in the
// corpus bakes to what the pack layer resolves is checked in corpus_test.cpp, beside the
// corpus sweep that already loads every pack.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "corpus.h"
#include "pack/attributes.h"
#include "pack/source.h"
#include "pack/xml_reader.h"
#include "viewer/live/bake.h"
#include "viewer/live/effective_values.h"

using Catch::Approx;

namespace {

struct Loaded {
    std::unique_ptr<pack::DataSource> source;
    pack::Pack model;
};

Loaded loadPath(const std::filesystem::path& path) {
    Loaded out;
    std::string error;
    out.source = pack::open(path, &error);
    REQUIRE(out.source != nullptr);
    out.model = pack::loadPack(*out.source);
    return out;
}

// A one-file pack: `body` goes inside <OverlayData>, after one category "c".
Loaded loadXml(const corpus::TempDir& temp, const std::string& body) {
    std::ofstream(temp / "markers.xml") << "<OverlayData>\n<MarkerCategory name=\"c\" DisplayName=\"C\"/>\n"
                                        << body << "\n</OverlayData>\n";
    return loadPath(temp.path());
}

} // namespace

// Pathing's Populate_Position: Hero's pack has hidden anchors with no position and their map id
// from their category, which its HT simulation finds by guid. Dropping them failed its script.
TEST_CASE("a POI without a full position is baked with 0 for what it lacks, its map its category's", "[bake][regression]") {
    const corpus::TempDir temp("bake_position");
    const Loaded loaded = loadXml(temp, R"(<MarkerCategory name="m" mapid="1437" heightoffset="0"/>
    <POIs>
        <POI type="c" xpos="1" ypos="2" zpos="3"/>
        <POI type="m" xpos="1" ypos="2"/>
        <POI type="m" xpos="1" ypos="two" zpos="3"/>
        <POI type="m"/>
    </POIs>)");

    const viewer::BakedPathables baked = viewer::bake(loaded.model);
    REQUIRE(baked.markers.size() == 4);
    CHECK(baked.markers[0].category == loaded.model.pathables[0].category);
    CHECK(baked.markers[1].position == glm::vec3(1, 2, 0));
    CHECK(baked.markers[2].position == glm::vec3(1, 0, 3));
    CHECK(baked.markers[3].position == glm::vec3(0, 0, 0));
    CHECK(baked.markers[3].map_id == 1437);
}

TEST_CASE("heightoffset lifts where a marker is drawn", "[bake]") {
    const corpus::TempDir temp("bake_height");
    const Loaded loaded = loadXml(temp, R"(<POIs><POI type="c" xpos="1" ypos="2" zpos="3" heightOffset="1.5"/></POIs>)");

    const viewer::BakedPathables baked = viewer::bake(loaded.model);
    REQUIRE(baked.markers.size() == 1);
    CHECK(baked.markers[0].position.y == Approx(3.5f));
}

TEST_CASE("a partial rotate is a rotation, the way Blish reads it", "[bake][regression]") {
    const corpus::TempDir temp("bake_rotate");
    const Loaded loaded = loadXml(temp, R"(<POIs>
        <POI type="c" xpos="0" ypos="0" zpos="0" rotate="90"/>
        <POI type="c" xpos="0" ypos="0" zpos="0" rotate="0,0,0"/>
        <POI type="c" xpos="0" ypos="0" zpos="0" rotate="10,20,30" rotate-y="45"/>
        <POI type="c" xpos="0" ypos="0" zpos="0"/>
    </POIs>)");

    const viewer::BakedPathables baked = viewer::bake(loaded.model);
    REQUIRE(baked.markers.size() == 4);

    CHECK(baked.markers[0].has_rotation);
    CHECK(baked.markers[0].rotation.x == Approx(3.14159265f / 2.0f));
    CHECK(baked.markers[0].rotation.y == Approx(0.0f));

    // Zero is still a rotation: it turns the billboard into a quad lying flat.
    CHECK(baked.markers[1].has_rotation);

    // The per-axis form is applied after the list, and wins.
    CHECK(baked.markers[2].rotation.y == Approx(3.14159265f / 4.0f));
    CHECK(baked.markers[2].rotation.z == Approx(3.14159265f / 6.0f));

    CHECK_FALSE(baked.markers[3].has_rotation);
}

TEST_CASE("a trail fades at 700 and 900 unless it says otherwise", "[bake]") {
    const corpus::TempDir temp("bake_trail_fade");
    const Loaded loaded = loadXml(temp, R"(<POIs>
        <Trail type="c" trailData="a.trl"/>
        <Trail type="c" trailData="b.trl" fadeNear="100" fadeFar="200" animSpeed="2"/>
    </POIs>)");

    const viewer::BakedPathables baked = viewer::bake(loaded.model);
    REQUIRE(baked.trails.size() == 2);
    CHECK(baked.trails[0].fade_near == Approx(700.0f));
    CHECK(baked.trails[0].fade_far == Approx(900.0f));
    CHECK(baked.trails[0].anim_speed == Approx(1.0f));
    CHECK(baked.trails[0].map_id == viewer::kMapUnknown); // the .trl decides, and it is unread
    CHECK(baked.trails[1].fade_near == Approx(100.0f));
    CHECK(baked.trails[1].fade_far == Approx(200.0f));
    CHECK(baked.trails[1].anim_speed == Approx(2.0f));
}

TEST_CASE("occlusion is inherited, and anything but its three values leaves it to the player", "[bake]") {
    const corpus::TempDir temp("bake_occlusion");
    const Loaded loaded = loadXml(temp, R"(<MarkerCategory name="walls" occlusion="Scene"/>
        <MarkerCategory name="free"/>
        <POIs>
            <POI type="walls" xpos="0" ypos="0" zpos="0"/>
            <POI type="walls" xpos="0" ypos="0" zpos="0" occlusion="none"/>
            <POI type="walls" xpos="0" ypos="0" zpos="0" occlusion="CHARACTERS"/>
            <POI type="free" xpos="0" ypos="0" zpos="0" occlusion="sky"/>
            <POI type="free" xpos="0" ypos="0" zpos="0" occlusion=""/>
            <POI type="free" xpos="0" ypos="0" zpos="0" occlusion="terrain" render-layer="windows"/>
            <Trail type="walls" trailData="a.trl"/>
        </POIs>)");

    using viewer::Occlusion;
    const viewer::BakedPathables baked = viewer::bake(loaded.model);
    REQUIRE(baked.markers.size() == 6);
    CHECK(baked.markers[0].occlusion == Occlusion::Scene);
    CHECK(baked.markers[1].occlusion == Occlusion::None);
    CHECK(baked.markers[2].occlusion == Occlusion::Characters);
    CHECK_FALSE(baked.markers[3].occlusion);
    CHECK_FALSE(baked.markers[4].occlusion);
    // v2.2.0's render-layer and its words are not read.
    CHECK_FALSE(baked.markers[5].occlusion);
    REQUIRE(baked.trails.size() == 1);
    CHECK(baked.trails[0].occlusion == Occlusion::Scene);
}

TEST_CASE("every occlusion the editor offers is one the viewer acts on, in Marker occlusion's order", "[bake]") {
    // The editor's dropdown is the table's `choices`; parseOcclusion is what reads them.
    const pack::AttributeDef* def = pack::findAttribute("occlusion");
    REQUIRE(def != nullptr);
    const std::vector<viewer::Occlusion> expected = {
        viewer::Occlusion::None,
        viewer::Occlusion::Characters,
        viewer::Occlusion::Scene,
    };
    REQUIRE(def->choices.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        INFO("choice " << def->choices[i]);
        CHECK(viewer::parseOcclusion(def->choices[i]) == expected[i]);
    }
}

TEST_CASE("cull is inherited, a name in any case, and anything else culls nothing", "[bake]") {
    const corpus::TempDir temp("bake_cull");
    const Loaded loaded = loadXml(temp, R"(<MarkerCategory name="walls" cull="counterclockwise"/>
        <MarkerCategory name="free"/>
        <POIs>
            <POI type="walls" xpos="0" ypos="0" zpos="0"/>
            <POI type="walls" xpos="0" ypos="0" zpos="0" cull="Clockwise"/>
            <POI type="free" xpos="0" ypos="0" zpos="0" cull="2"/>
            <POI type="free" xpos="0" ypos="0" zpos="0" cull=" Clockwise"/>
            <Trail type="walls" trailData="a.trl"/>
        </POIs>)");

    const viewer::BakedPathables baked = viewer::bake(loaded.model);
    REQUIRE(baked.markers.size() == 4);
    CHECK(baked.markers[0].cull == viewer::Cull::CounterClockwise);
    CHECK(baked.markers[1].cull == viewer::Cull::Clockwise);
    CHECK(baked.markers[2].cull == viewer::Cull::None); // by name only, as Pathing's enum lookup
    CHECK(baked.markers[3].cull == viewer::Cull::None); // untrimmed
    REQUIRE(baked.trails.size() == 1);
    CHECK(baked.trails[0].cull == viewer::Cull::CounterClockwise);
}

// The bake reads a table of each category's values rather than walking the chain: the nearest
// declaration wins, a pathable's own over all of them, a category split across two declarations
// takes both, and a missing value is the table's default.
TEST_CASE("what the bake inherits is the nearest value up the category chain", "[bake]") {
    const corpus::TempDir temp("bake_inherit");
    const Loaded loaded = loadXml(temp, R"(<MarkerCategory name="a" iconSize="2" alpha="0.5" mapid="15">
        <MarkerCategory name="b" iconsize="3">
            <MarkerCategory name="d" mapid="50"/>
        </MarkerCategory>
    </MarkerCategory>
    <MarkerCategory name="a"><MarkerCategory name="b" fadeFar="900"/></MarkerCategory>
    <POIs>
        <POI type="a.b.d" xpos="0" ypos="0" zpos="0"/>
        <POI type="a.b.d" xpos="0" ypos="0" zpos="0" IconSize="4"/>
        <POI type="a" xpos="0" ypos="0" zpos="0"/>
        <POI type="missing" xpos="0" ypos="0" zpos="0"/>
    </POIs>)");

    const viewer::EffectiveTable table(loaded.model);
    for (const pack::Pathable& pathable : loaded.model.pathables) {
        CHECK(table.of(pathable).values == viewer::effectiveValues(loaded.model, pathable).values);
    }

    const viewer::BakedPathables baked = viewer::bake(loaded.model);
    REQUIRE(baked.markers.size() == 4);
    CHECK(baked.markers[0].icon_size == 3.0f);
    CHECK(baked.markers[0].map_id == 50);
    CHECK(baked.markers[0].alpha == 0.5f);
    CHECK(baked.markers[0].fade_far == 900.0f);
    CHECK(baked.markers[1].icon_size == 4.0f);
    CHECK(baked.markers[2].icon_size == 2.0f);
    CHECK(baked.markers[2].map_id == 15);
    CHECK(baked.markers[2].fade_far == pack::defaultFloat("fadefar"));
    CHECK(baked.markers[3].icon_size == pack::defaultFloat("iconsize"));
    CHECK(baked.markers[3].alpha == pack::defaultFloat("alpha"));
}

TEST_CASE("tint and color are the same attribute", "[bake]") {
    const corpus::TempDir temp("bake_tint");
    const Loaded loaded = loadXml(temp, R"(<POIs>
        <POI type="c" xpos="0" ypos="0" zpos="0" color="FF0000"/>
        <POI type="c" xpos="0" ypos="0" zpos="0" tint="00FF00"/>
    </POIs>)");

    const viewer::BakedPathables baked = viewer::bake(loaded.model);
    REQUIRE(baked.markers.size() == 2);
    CHECK(baked.markers[0].tint == IM_COL32(255, 0, 0, 255));
    CHECK(baked.markers[1].tint == IM_COL32(0, 255, 0, 255));
}

TEST_CASE("a marker's size on the map is inherited, 20 unless it says otherwise", "[bake]") {
    const corpus::TempDir temp("bake_map_size");
    std::ofstream(temp / "markers.xml") << R"(<OverlayData>
        <MarkerCategory name="c" mapDisplaySize="32" scaleOnMapWithZoom="0"/>
        <MarkerCategory name="d"/>
        <POIs>
            <POI type="c" xpos="0" ypos="0" zpos="0"/>
            <POI type="d" xpos="0" ypos="0" zpos="0"/>
            <POI type="d" xpos="0" ypos="0" zpos="0" mapDisplaySize="8"/>
        </POIs>
    </OverlayData>)";
    const Loaded loaded = loadPath(temp.path());

    const viewer::BakedPathables baked = viewer::bake(loaded.model);
    REQUIRE(baked.markers.size() == 3);
    CHECK(baked.markers[0].map_display_size == 32.0f);
    CHECK_FALSE(baked.markers[0].scale_on_map_with_zoom);
    CHECK(baked.markers[1].map_display_size == 20.0f);
    CHECK(baked.markers[1].scale_on_map_with_zoom);
    CHECK(baked.markers[2].map_display_size == 8.0f);
}

TEST_CASE("a fade is linear between its bounds and off when either is negative", "[bake]") {
    CHECK(viewer::fadeFactor(50.0f, 100.0f, 200.0f) == Approx(1.0f));
    CHECK(viewer::fadeFactor(150.0f, 100.0f, 200.0f) == Approx(0.5f));
    CHECK(viewer::fadeFactor(250.0f, 100.0f, 200.0f) == Approx(0.0f));
    CHECK(viewer::fadeFactor(1e9f, -1.0f, 200.0f) == Approx(1.0f));
    CHECK(viewer::fadeFactor(1e9f, -1.0f, -1.0f) == Approx(1.0f));
}

TEST_CASE("category flags are read the way TmfLib reads them", "[bake]") {
    const corpus::TempDir temp("bake_categories");
    std::ofstream(temp / "markers.xml") << R"(<OverlayData>
        <MarkerCategory name="a" DisplayName="Alpha" isSeparator="1" defaultToggle="0" isHidden="1">
            <MarkerCategory name="b" isSeparator="true" defaultToggle="false"/>
        </MarkerCategory>
        <MarkerCategory name="c"/>
    </OverlayData>)";
    const Loaded loaded = loadPath(temp.path());

    const std::vector<viewer::BakedCategory> baked = viewer::bakeCategories(loaded.model);
    REQUIRE(baked.size() == 3);

    CHECK(baked[0].id == "a");
    CHECK(baked[0].display_name == "Alpha");
    CHECK(baked[0].is_separator == true);
    CHECK(baked[0].default_toggle == false);
    CHECK(baked[0].is_hidden == true);

    // Only "1" makes a separator, and only "0" turns a default off: "true" and "false" are
    // neither, so a separator stays off and the toggle stays on, as in Pathing.
    CHECK(baked[1].id == "a.b");
    CHECK(baked[1].parent == 0);
    CHECK(baked[1].is_separator == false);
    CHECK(baked[1].default_toggle == true);

    // Saying nothing is not saying no: a merge has to keep what another pack said.
    CHECK_FALSE(baked[2].display_name.has_value());
    CHECK_FALSE(baked[2].is_separator.has_value());
    CHECK_FALSE(baked[2].default_toggle.has_value());
}

TEST_CASE("a category's achievement is its own id with the bit it inherits", "[bake]") {
    const corpus::TempDir temp("bake_category_achievement");
    std::ofstream(temp / "markers.xml") << R"(<OverlayData>
        <MarkerCategory name="a" achievementId="1234" achievementBit="3">
            <MarkerCategory name="b" achievementId="5678"/>
            <MarkerCategory name="c" achievementBit="x"/>
        </MarkerCategory>
        <MarkerCategory name="d" achievementId="not a number"/>
    </OverlayData>)";
    const Loaded loaded = loadPath(temp.path());

    const std::vector<viewer::BakedCategory> baked = viewer::bakeCategories(loaded.model);
    REQUIRE(baked.size() == 4);
    CHECK(baked[0].achievement_id == 1234);
    CHECK(baked[0].achievement_bit == 3);
    CHECK(baked[1].achievement_id == 5678); // the id is only ever its own
    CHECK(baked[1].achievement_bit == 3); // the bit is inherited
    CHECK_FALSE(baked[2].achievement_id.has_value());
    CHECK(baked[2].achievement_bit == -1); // its own bit will not parse, and stops the search
    CHECK_FALSE(baked[3].achievement_id.has_value());
}
