// The viewer's settings file: what is missing or broken keeps its default.

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <string>

#include "corpus.h"
#include "util/json_file.h"
#include "viewer/settings.h"

using corpus::writeFile;

TEST_CASE("a broken settings file leaves the defaults standing", "[viewer][settings]") {
    const corpus::TempDir temp("settings_broken");
    writeFile(temp / viewer::settings::FILE_NAME, "{\"run_scripts\": fa");
    CHECK(viewer::settings::load(temp.path()).run_scripts);

    writeFile(temp / viewer::settings::FILE_NAME, "{\"run_scripts\": \"no\"}");
    CHECK(viewer::settings::load(temp.path()).run_scripts);

    writeFile(temp / viewer::settings::FILE_NAME, "[false]");
    CHECK(viewer::settings::load(temp.path()).run_scripts);
}

TEST_CASE("a file from before Marker occlusion keeps what its checkbox said", "[viewer][settings][regression]") {
    // "Hide markers behind terrain" on was behind the whole scene; off, or absent, behind nothing.
    const corpus::TempDir temp("settings_occlusion");
    writeFile(temp / viewer::settings::FILE_NAME, "{\"behind_terrain\": true}");
    CHECK(viewer::settings::load(temp.path()).occlusion == 2);
    writeFile(temp / viewer::settings::FILE_NAME, "{\"behind_terrain\": false}");
    CHECK(viewer::settings::load(temp.path()).occlusion == 0);

    // Once saved, the dropdown's own value wins, and one out of range is clamped.
    writeFile(temp / viewer::settings::FILE_NAME, "{\"behind_terrain\": true, \"occlusion\": 1}");
    CHECK(viewer::settings::load(temp.path()).occlusion == 1);
    writeFile(temp / viewer::settings::FILE_NAME, "{\"occlusion\": 7}");
    CHECK(viewer::settings::load(temp.path()).occlusion == 2);
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

TEST_CASE("the menu starts behind our icon only, and where it is shown is kept", "[viewer][settings]") {
    const corpus::TempDir temp("settings_menu");
    const viewer::settings::Settings defaults = viewer::settings::load(temp.path());
    CHECK_FALSE(defaults.menu_in_nexus);
    CHECK(defaults.menu_icon);

    viewer::settings::Settings settings;
    settings.menu_in_nexus = true;
    settings.menu_icon = false;
    REQUIRE(viewer::settings::save(temp.path(), settings));
    CHECK(viewer::settings::load(temp.path()).menu_in_nexus);
    CHECK_FALSE(viewer::settings::load(temp.path()).menu_icon);
}

TEST_CASE("behavior settings start at Pathing's defaults and are kept", "[viewer][settings]") {
    const corpus::TempDir temp("settings_behaviors");
    const viewer::settings::Settings defaults = viewer::settings::load(temp.path());
    CHECK(defaults.allow_auto_hide);
    CHECK(defaults.allow_animation);
    CHECK(defaults.smart_categories);
    CHECK(defaults.occlusion == 0);
    CHECK(defaults.show_interact_prompt);
    CHECK(defaults.clipboard_consent == 0);

    viewer::settings::Settings settings;
    settings.allow_auto_hide = false;
    settings.allow_animation = false;
    settings.smart_categories = false;
    settings.occlusion = 1;
    settings.show_interact_prompt = false;
    settings.clipboard_consent = 2;
    REQUIRE(viewer::settings::save(temp.path(), settings));
    const viewer::settings::Settings loaded = viewer::settings::load(temp.path());
    CHECK_FALSE(loaded.allow_auto_hide);
    CHECK_FALSE(loaded.allow_animation);
    CHECK_FALSE(loaded.smart_categories);
    CHECK(loaded.occlusion == 1);
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
    CHECK_FALSE(defaults.map_above_below); // off, unlike Pathing
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

TEST_CASE("the maps' DPI scaling is Nexus's unless chosen otherwise, and is kept", "[viewer][settings]") {
    const corpus::TempDir temp("settings_dpi");
    const viewer::settings::Settings defaults = viewer::settings::load(temp.path());
    CHECK(static_cast<viewer::settings::DpiScaling>(defaults.map_dpi_scaling) == viewer::settings::DpiScaling::Nexus);
    CHECK(defaults.map_dpi_custom == 1.0f);

    viewer::settings::Settings settings;
    settings.map_dpi_scaling = static_cast<int>(viewer::settings::DpiScaling::Custom);
    settings.map_dpi_custom = 1.25f;
    REQUIRE(viewer::settings::save(temp.path(), settings));
    const viewer::settings::Settings loaded = viewer::settings::load(temp.path());
    CHECK(static_cast<viewer::settings::DpiScaling>(loaded.map_dpi_scaling) == viewer::settings::DpiScaling::Custom);
    CHECK(loaded.map_dpi_custom == 1.25f);

    std::ofstream(temp / viewer::settings::FILE_NAME) << R"({"map_dpi_scaling": 9, "map_dpi_custom": 0})";
    const viewer::settings::Settings clamped = viewer::settings::load(temp.path());
    CHECK(static_cast<viewer::settings::DpiScaling>(clamped.map_dpi_scaling) == viewer::settings::DpiScaling::Custom);
    CHECK(clamped.map_dpi_custom == 0.5f);
}

TEST_CASE("no folder means nothing is read and nothing is written", "[viewer][settings]") {
    CHECK(viewer::settings::load({}).run_scripts);
    CHECK_FALSE(viewer::settings::save({}, viewer::settings::Settings{}));
}

TEST_CASE("a kept file is replaced whole, or left as it was", "[viewer][settings]") {
    const corpus::TempDir temp("settings_write");
    const std::filesystem::path file = temp / "kept.json";
    REQUIRE(util::json_file::writeText(file, "first"));
    REQUIRE(util::json_file::writeText(file, "second"));
    std::ifstream in(file, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(in), {}) == "second");
    CHECK_FALSE(std::filesystem::exists(temp / "kept.json.tmp"));

    // A file that cannot be replaced — here a folder in its place — stays, and so does nothing
    // half written beside it.
    std::filesystem::create_directories(temp / "blocked.json" / "inside");
    CHECK_FALSE(util::json_file::writeText(temp / "blocked.json", "text"));
    CHECK(std::filesystem::is_directory(temp / "blocked.json" / "inside"));
    CHECK_FALSE(std::filesystem::exists(temp / "blocked.json.tmp"));
}

TEST_CASE("slow frames are not logged unless asked, and the threshold is kept within its range", "[viewer][settings]") {
    const corpus::TempDir temp("settings_slow_frames");
    const viewer::settings::Settings defaults = viewer::settings::load(temp.path());
    CHECK_FALSE(defaults.log_slow_frames);
    CHECK(defaults.slow_frame_ms == 5.0f);

    viewer::settings::Settings settings;
    settings.log_slow_frames = true;
    settings.slow_frame_ms = 2.5f;
    REQUIRE(viewer::settings::save(temp.path(), settings));
    viewer::settings::Settings loaded = viewer::settings::load(temp.path());
    CHECK(loaded.log_slow_frames);
    CHECK(loaded.slow_frame_ms == 2.5f);

    settings.slow_frame_ms = 0.0f; // hand-edited: every frame would be slow
    REQUIRE(viewer::settings::save(temp.path(), settings));
    loaded = viewer::settings::load(temp.path());
    CHECK(loaded.slow_frame_ms == 1.0f);
}
