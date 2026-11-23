// The Blish HUD import: what Pathing kept beside its packs, read from a markers folder laid out
// in the test as Pathing lays it out, and the copy into ours.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "corpus.h"
#include "viewer/behaviors/states.h"
#include "viewer/blish_import.h"
#include "viewer/store.h"

namespace {

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("blish import: packs at any depth, and what Pathing kept beside them", "[blish_import]") {
    const corpus::TempDir temp("blish_find");
    corpus::writeFile(temp / "Hero.zip", "zip");
    corpus::writeFile(temp / "more" / "Teh.taco", "taco");
    corpus::writeFile(temp / "loose.xml", "<OverlayData/>");
    corpus::writeFile(temp / "data" / "states" / "category_preferences.txt", "+Tw_Guides.Hero\n-tw_raids\r\n\n-\n");
    corpus::writeFile(temp / "data" / "kv" / "kv.json", R"({"hero.progress": "12", "skipped": 3})");
    corpus::writeFile(temp / "data" / "states" / "timers.txt", "");

    const viewer::blish_import::Found found = viewer::blish_import::find(temp.path());
    // The folder is a pack of its own for its loose XML, but it is Blish HUD's folder: not copied.
    REQUIRE(found.packs.size() == 2);
    CHECK(found.packs[0].filename() == "Hero.zip");
    CHECK(found.packs[1].filename() == "Teh.taco");

    REQUIRE(found.categories.size() == 2);
    CHECK(found.categories[0] == std::pair<std::string, bool>{"tw_guides.hero", true});
    CHECK(found.categories[1] == std::pair<std::string, bool>{"tw_raids", false});

    REQUIRE(found.storage.size() == 1);
    CHECK(found.storage[0] == std::pair<std::string, std::string>{"hero.progress", "12"});
    CHECK(found.timers == temp / "data" / "states" / "timers.txt");
}

TEST_CASE("blish import: Pathing's older category files, the inverted one winning", "[blish_import]") {
    const corpus::TempDir temp("blish_legacy");
    corpus::writeFile(temp / "categories.txt", "a\nb\n");
    corpus::writeFile(temp / "invcategories.txt", "b\nc\n");

    const auto choices = viewer::blish_import::readCategoryChoices(temp.path());
    REQUIRE(choices.size() == 3);
    CHECK(choices[0] == std::pair<std::string, bool>{"a", false});
    CHECK(choices[1] == std::pair<std::string, bool>{"b", true});
    CHECK(choices[2] == std::pair<std::string, bool>{"c", true});
}

TEST_CASE("blish import: no folder finds nothing", "[blish_import]") {
    CHECK(viewer::blish_import::find("Z:/no/such/markers").empty());
    CHECK(viewer::blish_import::find({}).empty());
}

TEST_CASE("blish import: a pack is copied unless ours is as new", "[blish_import]") {
    const corpus::TempDir temp("blish_copy");
    corpus::writeFile(temp / "blish" / "Hero.zip", "new hero");
    corpus::writeFile(temp / "blish" / "Teh.taco", "teh");
    corpus::writeFile(temp / "ours" / "Teh.taco", "our teh");
    // Ours is the newer one.
    std::filesystem::last_write_time(
        temp / "ours" / "Teh.taco",
        std::filesystem::last_write_time(temp / "blish" / "Teh.taco") + std::chrono::hours(1));

    const viewer::blish_import::Copied copied = viewer::blish_import::copyPacks(
        {temp / "blish" / "Hero.zip", temp / "blish" / "Teh.taco", temp / "blish" / "Gone.zip"},
        temp / "ours");
    CHECK(copied.copied == 1);
    CHECK(copied.kept == 1);
    REQUIRE(copied.errors.size() == 1);
    CHECK(copied.errors[0].rfind("Gone.zip", 0) == 0);

    CHECK(read(temp / "ours" / "Hero.zip") == "new hero");
    CHECK(read(temp / "ours" / "Teh.taco") == "our teh");
    CHECK_FALSE(std::filesystem::exists(temp / "ours" / "Gone.zip.part"));

    // Copied with its time, so a second import keeps it.
    CHECK(viewer::blish_import::copyPacks({temp / "blish" / "Hero.zip"}, temp / "ours").kept == 1);
}

TEST_CASE("blish import: two packs of one name in different folders are not both counted", "[blish_import]") {
    const corpus::TempDir temp("blish_same_name");
    corpus::writeFile(temp / "blish" / "a" / "Tekkit.zip", "first");
    corpus::writeFile(temp / "blish" / "b" / "tekkit.zip", "second");

    const viewer::blish_import::Copied copied =
        viewer::blish_import::copyPacks({temp / "blish" / "a" / "Tekkit.zip", temp / "blish" / "b" / "tekkit.zip"}, temp / "ours");
    CHECK(copied.copied == 1);
    CHECK(copied.kept == 0);
    REQUIRE(copied.errors.size() == 1);
    CHECK(copied.errors[0].find("tekkit.zip") != std::string::npos);
    CHECK(read(temp / "ours" / "Tekkit.zip") == "first");
}

TEST_CASE("blish import: a choice is kept as Pathing made it, until set to the default", "[blish_import]") {
    viewer::store::CategoryStates states;
    states.remember("a", true);
    CHECK(states.enabled("a", false));
    CHECK(states.enabled("a", true));
    states.set("a", true, true); // equal to the default now: forgotten
    CHECK_FALSE(states.enabled("a", false));
}

TEST_CASE("blish import: Pathing's timers merge with ours, the later expiry winning", "[blish_import]") {
    const corpus::TempDir temp("blish_timers");
    const std::string guid = "0b1f1b0a-1d2c-4e5f-8a9b-0c1d2e3f4a5b";
    const std::string other = "1b1f1b0a-1d2c-4e5f-8a9b-0c1d2e3f4a5b";
    corpus::writeFile(temp / "ours.txt", guid + ",t2030-01-01T00:00:00\n");
    corpus::writeFile(temp / "theirs.txt", guid + ",t2031-01-01T00:00:00\n" + other + ",t2029-01-01T00:00:00\n");

    viewer::behaviors::BehaviorStates ours;
    ours.load(temp / "ours.txt");
    viewer::behaviors::BehaviorStates theirs;
    theirs.load(temp / "theirs.txt");
    ours.merge(theirs);
    CHECK(ours.dirty());

    REQUIRE(ours.save(temp / "merged.txt"));
    const std::string merged = read(temp / "merged.txt");
    CHECK(merged.find(guid + ",t2031-01-01T00:00:00") != std::string::npos);
    CHECK(merged.find(other + ",t2029-01-01T00:00:00") != std::string::npos);
}
