// The editor's settings file, written and read back.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "corpus.h"
#include "editor/settings.h"
#include "util/path_text.h"

namespace {

std::string directory;

const char* addonDirectory(const char*) {
    return directory.c_str();
}

} // namespace

TEST_CASE("the last folders survive a non-ASCII name", "[editor][settings]") {
    // "Jörg Łódź": ö is in the Western code page and comes out of path::string() as a byte that
    // is not UTF-8, which nlohmann throws on; Ł is in none, and string() itself throws.
    const corpus::TempDir temp("editor_settings");
    directory = temp.path().string();
    AddonAPI api{};
    api.Paths.GetAddonDirectory = addonDirectory;
    settings::init(&api);

    const std::filesystem::path folder = temp.path() / util::pathFromText("J\xC3\xB6rg \xC5\x81\xC3\xB3" "d\xC5\xBA");
    const std::filesystem::path images = folder / util::pathFromText("\xC3\xA9");
    settings::update(editor::Settings{}, folder, images);

    editor::Settings read_settings;
    std::filesystem::path read_folder;
    std::filesystem::path read_images;
    settings::load(&read_settings, &read_folder, &read_images);
    CHECK(read_folder == folder);
    CHECK(read_images == images);
}
