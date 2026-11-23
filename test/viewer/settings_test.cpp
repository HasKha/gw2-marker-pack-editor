// The viewer's settings file, and above all the one rule in it that decides who gets the
// editor: an install that already had it keeps it, a new one does not.

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("the Blish HUD folder is read, and scripts run, unless turned off", "[viewer][settings]") {
    const corpus::TempDir temp("settings_blish");
    CHECK(viewer::settings::load(temp.path()).read_blish_folder);
    CHECK(viewer::settings::load(temp.path()).run_scripts);

    viewer::settings::Settings settings;
    settings.read_blish_folder = false;
    settings.run_scripts = false;
    REQUIRE(viewer::settings::save(temp.path(), settings));
    CHECK_FALSE(viewer::settings::load(temp.path()).read_blish_folder);
    CHECK_FALSE(viewer::settings::load(temp.path()).run_scripts);
}

TEST_CASE("behavior settings start at Pathing's defaults and are kept", "[viewer][settings]") {
    const corpus::TempDir temp("settings_behaviors");
    const viewer::settings::Settings defaults = viewer::settings::load(temp.path());
    CHECK(defaults.allow_auto_hide);
    CHECK(defaults.allow_animation);
    CHECK(defaults.show_interact_prompt);
    CHECK(defaults.clipboard_consent == 0);
    CHECK(defaults.interact_key == 'F');

    viewer::settings::Settings settings;
    settings.allow_auto_hide = false;
    settings.allow_animation = false;
    settings.show_interact_prompt = false;
    settings.clipboard_consent = 2;
    settings.interact_key = 'G';
    REQUIRE(viewer::settings::save(temp.path(), settings));
    const viewer::settings::Settings loaded = viewer::settings::load(temp.path());
    CHECK_FALSE(loaded.allow_auto_hide);
    CHECK_FALSE(loaded.allow_animation);
    CHECK_FALSE(loaded.show_interact_prompt);
    CHECK(loaded.clipboard_consent == 2);
    CHECK(loaded.interact_key == 'G');
}

TEST_CASE("no folder means nothing is read and nothing is written", "[viewer][settings]") {
    CHECK_FALSE(viewer::settings::load({}).enable_editing);
    CHECK_FALSE(viewer::settings::save({}, viewer::settings::Settings{true}));
}
