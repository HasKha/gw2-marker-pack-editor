// The viewer's settings file, and above all the one rule in it that decides who gets the
// editor: an install that already had it keeps it, a new one does not.

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

#include "corpus.h"
#include "viewer/settings.h"

using corpus::writeFile;

TEST_CASE("a new install starts with editing off", "[viewer][settings]") {
    const corpus::TempDir temp("settings_new");
    CHECK_FALSE(viewer::settings::load(temp.path()).enable_editing);
}

TEST_CASE("an install that had the editor keeps it", "[viewer][settings]") {
    const corpus::TempDir temp("settings_legacy");
    writeFile(temp / viewer::settings::EDITOR_FILE_NAME, "{}");
    CHECK(viewer::settings::load(temp.path()).enable_editing);
}

TEST_CASE("a saved choice wins over the editor's file", "[viewer][settings]") {
    const corpus::TempDir temp("settings_saved");
    writeFile(temp / viewer::settings::EDITOR_FILE_NAME, "{}");

    REQUIRE(viewer::settings::save(temp.path(), viewer::settings::Settings{false}));
    CHECK_FALSE(viewer::settings::load(temp.path()).enable_editing);

    REQUIRE(viewer::settings::save(temp.path(), viewer::settings::Settings{true}));
    CHECK(viewer::settings::load(temp.path()).enable_editing);
}

TEST_CASE("a broken settings file leaves the defaults standing", "[viewer][settings]") {
    const corpus::TempDir temp("settings_broken");
    writeFile(temp / viewer::settings::EDITOR_FILE_NAME, "{}");

    writeFile(temp / viewer::settings::FILE_NAME, "{\"enable_editing\": tr");
    CHECK(viewer::settings::load(temp.path()).enable_editing);

    writeFile(temp / viewer::settings::FILE_NAME, "{\"enable_editing\": \"no\"}");
    CHECK(viewer::settings::load(temp.path()).enable_editing);

    writeFile(temp / viewer::settings::FILE_NAME, "[false]");
    CHECK(viewer::settings::load(temp.path()).enable_editing);
}

TEST_CASE("the Blish HUD import is offered until it has been, and scripts run unless turned off", "[viewer][settings]") {
    const corpus::TempDir temp("settings_blish");
    CHECK_FALSE(viewer::settings::load(temp.path()).import_offered);
    CHECK(viewer::settings::load(temp.path()).run_scripts);

    viewer::settings::Settings settings;
    settings.import_offered = true;
    settings.run_scripts = false;
    REQUIRE(viewer::settings::save(temp.path(), settings));
    CHECK(viewer::settings::load(temp.path()).import_offered);
    CHECK_FALSE(viewer::settings::load(temp.path()).run_scripts);
}

TEST_CASE("behavior settings start at Pathing's defaults and are kept", "[viewer][settings]") {
    const corpus::TempDir temp("settings_behaviors");
    const viewer::settings::Settings defaults = viewer::settings::load(temp.path());
    CHECK(defaults.allow_auto_hide);
    CHECK(defaults.allow_animation);
    CHECK(defaults.show_interact_prompt);
    CHECK(defaults.clipboard_consent == 0);

    viewer::settings::Settings settings;
    settings.allow_auto_hide = false;
    settings.allow_animation = false;
    settings.show_interact_prompt = false;
    settings.clipboard_consent = 2;
    REQUIRE(viewer::settings::save(temp.path(), settings));
    const viewer::settings::Settings loaded = viewer::settings::load(temp.path());
    CHECK_FALSE(loaded.allow_auto_hide);
    CHECK_FALSE(loaded.allow_animation);
    CHECK_FALSE(loaded.show_interact_prompt);
    CHECK(loaded.clipboard_consent == 2);
}

TEST_CASE("map and world settings start at Pathing's defaults and are kept", "[viewer][settings]") {
    const corpus::TempDir temp("settings_maps");
    const viewer::settings::Settings defaults = viewer::settings::load(temp.path());
    CHECK(defaults.map_pathables);
    CHECK(defaults.map_marker_visibility == 0);
    CHECK(defaults.minimap_trail_visibility == 0);
    CHECK(defaults.map_opacity == 1.0f);
    CHECK(defaults.minimap_opacity == 1.0f);
    CHECK(defaults.map_above_below);
    CHECK(defaults.map_fade_far_trails);
    CHECK(defaults.map_tooltips);
    CHECK(defaults.map_trail_width == 2.0f);
    CHECK(defaults.info_display == 0);
    CHECK(defaults.world_pathables);
    CHECK(defaults.max_opacity == 1.0f);
    CHECK(defaults.view_distance == 25000.0f);
    CHECK(defaults.max_trail_speed == 10.0f);
    CHECK(defaults.marker_scale == 1.0f);
    CHECK(defaults.combat_fade);

    viewer::settings::Settings settings;
    settings.map_pathables = false;
    settings.map_trail_visibility = 2;
    settings.minimap_marker_visibility = 1;
    settings.minimap_opacity = 0.25f;
    settings.map_trail_width = 4.0f;
    settings.map_tooltips = false;
    settings.info_display = 1;
    settings.view_distance = 300.0f;
    settings.marker_scale = 1.5f;
    settings.combat_fade = false;
    REQUIRE(viewer::settings::save(temp.path(), settings));
    const viewer::settings::Settings loaded = viewer::settings::load(temp.path());
    CHECK_FALSE(loaded.map_pathables);
    CHECK(loaded.map_trail_visibility == 2);
    CHECK(loaded.minimap_marker_visibility == 1);
    CHECK(loaded.minimap_opacity == 0.25f);
    CHECK(loaded.map_trail_width == 4.0f);
    CHECK_FALSE(loaded.map_tooltips);
    CHECK(loaded.info_display == 1);
    CHECK(loaded.view_distance == 300.0f);
    CHECK(loaded.marker_scale == 1.5f);
    CHECK_FALSE(loaded.combat_fade);
}

TEST_CASE("a hand-edited setting out of its range is brought back into it", "[viewer][settings]") {
    const corpus::TempDir temp("settings_ranges");
    std::ofstream(temp / viewer::settings::FILE_NAME)
        << R"({"map_opacity": 3, "map_trail_width": 0, "map_marker_visibility": 7, "clipboard_consent": -1, "map_tooltips": "no"})";
    const viewer::settings::Settings loaded = viewer::settings::load(temp.path());
    CHECK(loaded.map_opacity == 1.0f);
    CHECK(loaded.map_trail_width == 0.5f);
    CHECK(loaded.map_marker_visibility == 2);
    CHECK(loaded.clipboard_consent == 0);
    CHECK(loaded.map_tooltips); // not a boolean: the default stands
}

TEST_CASE("no folder means nothing is read and nothing is written", "[viewer][settings]") {
    CHECK_FALSE(viewer::settings::load({}).enable_editing);
    CHECK_FALSE(viewer::settings::save({}, viewer::settings::Settings{true}));
}
