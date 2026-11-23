// An achievement's details as /v2/achievements gives them and achievements.json keeps them.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "corpus.h"
#include "util/json_file.h"
#include "viewer/achievements.h"

using viewer::achievements::Details;

TEST_CASE("the game's markup comes out as CleanMessage takes it out", "[achievements]") {
    CHECK(viewer::achievements::withoutMarkup("Find <c=@flavor>the hidden</c> chest.") == "Find the hidden chest.");
    CHECK(viewer::achievements::withoutMarkup("<C=#FFAA00>Gold</c> and <c=@reminder>open") == "Gold and open");
    CHECK(viewer::achievements::withoutMarkup("One<br>Two<br/><BR />Three") == "One\nTwo\nThree");
}

TEST_CASE("details are read with each bit's text, and a text bit stands for the requirement", "[achievements]") {
    const auto details = viewer::achievements::detailsFromApi(R"([{
        "id": 100, "name": "Jumping Puzzle", "description": "<c=@flavor>High up.</c>",
        "requirement": "Reach the top.",
        "bits": [{"type": "Text", "text": "First ledge"}, {"type": "Item", "id": 5}, {"type": "Text", "text": "Summit"}]
    }])");
    REQUIRE(details);
    const Details& jp = details->at(100);
    CHECK(jp.name == "Jumping Puzzle");
    CHECK(jp.description == "High up.");
    CHECK(jp.bits == std::vector<std::string>{"First ledge", "", "Summit"});
    CHECK(jp.requirementFor(-1) == "Reach the top.");
    CHECK(jp.requirementFor(2) == "Summit");
    CHECK(jp.requirementFor(1) == "Reach the top."); // an item bit has no text
    CHECK(jp.requirementFor(9) == "Reach the top.");

    CHECK_FALSE(viewer::achievements::detailsFromApi(R"({"text":"all ids provided are invalid"})"));
}

TEST_CASE("the details cache keeps what the API does not know, and the catalog starts from it", "[achievements]") {
    viewer::achievements::Known known;
    known[100] = Details{"Jumping Puzzle", "High up.", "Reach the top.", {"First ledge"}};
    known[5] = std::nullopt;
    const corpus::TempDir temp("achievements_cache");
    const std::filesystem::path file = temp / viewer::achievements::CACHE_FILE_NAME;
    REQUIRE(util::json_file::write(file, viewer::achievements::toCache(known)));

    viewer::achievements::Catalog catalog;
    catalog.open(file);
    const Details* jp = catalog.find(100);
    REQUIRE(jp);
    CHECK(jp->requirementFor(0) == "First ledge");
    CHECK(catalog.find(5) == nullptr); // not an achievement: never asked for again
    CHECK(catalog.find(7) == nullptr); // not known yet

    catalog.clear();
    CHECK(catalog.find(100) == nullptr);
    CHECK_FALSE(std::filesystem::exists(file));
}
