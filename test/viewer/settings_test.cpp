// The viewer's settings file, and above all the one rule in it that decides who gets the
// editor: an install that already had it keeps it, a new one does not.

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

#include "corpus.h"
#include "viewer/settings.h"

namespace {

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::trunc);
    output << text;
}

} // namespace

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

TEST_CASE("no folder means nothing is read and nothing is written", "[viewer][settings]") {
    CHECK_FALSE(viewer::settings::load({}).enable_editing);
    CHECK_FALSE(viewer::settings::save({}, viewer::settings::Settings{true}));
}
