// The addon's folder following it across the rename, from MarkerPackEditor to Markus.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "corpus.h"
#include "viewer/store.h"

using corpus::writeFile;

TEST_CASE("the old folder moves into the empty one Nexus made", "[viewer][store]") {
    const corpus::TempDir temp("adopt_empty");
    writeFile(temp / "MarkerPackEditor" / "packs" / "a.zip", "zip");
    writeFile(temp / "MarkerPackEditor" / "viewer.json", "{}");
    std::filesystem::create_directories(temp / "Markus");

    CHECK(viewer::store::adoptFolder(temp / "MarkerPackEditor", temp / "Markus").empty());
    CHECK(std::filesystem::is_regular_file(temp / "Markus" / "packs" / "a.zip"));
    CHECK(std::filesystem::is_regular_file(temp / "Markus" / "viewer.json"));
    CHECK_FALSE(std::filesystem::exists(temp / "MarkerPackEditor"));
}

TEST_CASE("the old folder moves when the new one is missing", "[viewer][store]") {
    const corpus::TempDir temp("adopt_missing");
    writeFile(temp / "MarkerPackEditor" / "viewer.json", "{}");

    CHECK(viewer::store::adoptFolder(temp / "MarkerPackEditor", temp / "Markus").empty());
    CHECK(std::filesystem::is_regular_file(temp / "Markus" / "viewer.json"));
}

TEST_CASE("a new folder with something in it is left alone, and so is the old", "[viewer][store]") {
    const corpus::TempDir temp("adopt_taken");
    writeFile(temp / "MarkerPackEditor" / "viewer.json", "{\"old\": true}");
    writeFile(temp / "Markus" / "viewer.json", "{}");

    CHECK(viewer::store::adoptFolder(temp / "MarkerPackEditor", temp / "Markus").empty());
    CHECK(std::filesystem::is_regular_file(temp / "MarkerPackEditor" / "viewer.json"));
    CHECK(std::filesystem::file_size(temp / "Markus" / "viewer.json") == 2);
}

TEST_CASE("no old folder, nothing to do", "[viewer][store]") {
    const corpus::TempDir temp("adopt_none");
    std::filesystem::create_directories(temp / "Markus");

    CHECK(viewer::store::adoptFolder(temp / "MarkerPackEditor", temp / "Markus").empty());
    CHECK(std::filesystem::is_directory(temp / "Markus"));
}

TEST_CASE("a stored value that is not UTF-8 saves rather than throws", "[viewer][store]") {
    // Lua strings are bytes: a script that cuts "Zhé" short with string.sub stores half of é,
    // where Pathing's strings are characters and cannot hold that.
    const corpus::TempDir temp("kv_not_utf8");
    viewer::store::KeyValues storage;
    storage.upsert("tehstrails/name", "Zh\xC3");
    REQUIRE(storage.save(temp / "kv.json"));

    viewer::store::KeyValues read;
    read.load(temp / "kv.json");
    CHECK(read.read("tehstrails/name") == "Zh\xEF\xBF\xBD"); // U+FFFD in its place
}
