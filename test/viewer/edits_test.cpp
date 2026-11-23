// Personal copies: where Edit takes a pack, how it is copied, and how a copy stands in for the
// pack it was copied from.

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <string>

#include "corpus.h"
#include "viewer/edits.h"
#include "viewer/registry.h"

namespace {

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("edits: a pack is edited as its copy, a copy as itself", "[edits]") {
    const corpus::TempDir temp("edits_target");
    const std::filesystem::path edits = temp / "edits";
    corpus::writeFile(temp / "blish" / "Hero.zip", "zip");
    corpus::writeFile(edits / "Mine.zip", "zip");
    corpus::writeFile(temp / "blish" / "markers.xml", "<OverlayData/>");

    CHECK(viewer::edits::target(temp / "blish" / "Hero.zip", edits) == edits / "Hero.zip");
    CHECK(viewer::edits::target(edits / "Mine.zip", edits) == edits / "Mine.zip");
    // Blish HUD's folder as a pack of its own is never copied.
    CHECK(viewer::edits::target(temp / "blish", edits).empty());
}

TEST_CASE("edits: a copy lands whole, or not at all", "[edits]") {
    const corpus::TempDir temp("edits_copy");
    corpus::writeFile(temp / "blish" / "Hero.zip", "the pack's bytes");

    const std::filesystem::path to = temp / "edits" / "Hero.zip";
    CHECK(viewer::edits::copy(temp / "blish" / "Hero.zip", to).empty());
    CHECK(read(to) == "the pack's bytes");
    CHECK_FALSE(std::filesystem::exists(temp / "edits" / "Hero.zip.part"));

    CHECK_FALSE(viewer::edits::copy(temp / "blish" / "Gone.zip", temp / "edits" / "Gone.zip").empty());
    CHECK_FALSE(std::filesystem::exists(temp / "edits" / "Gone.zip"));
    CHECK_FALSE(std::filesystem::exists(temp / "edits" / "Gone.zip.part"));
}

TEST_CASE("edits: a personal copy stands in for ours, and ours for Blish HUD's", "[edits]") {
    const std::vector<std::filesystem::path> merged = viewer::registry::mergeSources({
        {"edits/Hero.zip"},
        {"ours/hero.zip", "ours/TehsTrails.taco"},
        {"blish/Hero.taco", "blish/tehstrails.zip", "blish/Other.zip"},
    });
    REQUIRE(merged.size() == 3);
    CHECK(merged[0] == "edits/Hero.zip");
    CHECK(merged[1] == "ours/TehsTrails.taco");
    CHECK(merged[2] == "blish/Other.zip");
}

TEST_CASE("edits: a tier never hides its own packs", "[edits]") {
    // A .zip and a .taco of one name in the same folder are both loaded, as Pathing loads both.
    const std::vector<std::filesystem::path> merged = viewer::registry::mergeSources({{"ours/a.zip", "ours/A.taco"}, {"blish/a.zip"}});
    CHECK(merged == std::vector<std::filesystem::path>{"ours/a.zip", "ours/A.taco"});
}

TEST_CASE("edits: inside a folder is by path, not by name", "[edits]") {
    CHECK(viewer::registry::isInside("C:/addons/edits/Hero.zip", "C:/addons/edits"));
    CHECK(viewer::registry::isInside("C:/addons/edits/sub/../Hero.zip", "C:/addons/edits/"));
    CHECK_FALSE(viewer::registry::isInside("C:/addons/edits2/Hero.zip", "C:/addons/edits"));
    CHECK_FALSE(viewer::registry::isInside("C:/addons/Hero.zip", "C:/addons/edits"));
    CHECK_FALSE(viewer::registry::isInside("C:/addons/Hero.zip", ""));
}
