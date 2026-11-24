// The pack catalog: what Pathing's repo.json lists, which of its packs are on disk and stale,
// the per-pack choices, and downloads run against a fetch of the test's own — nothing here
// reaches the web.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>
#include <thread>

#include "corpus.h"
#include "viewer/catalog.h"
#include "viewer/downloads.h"
#include "viewer/registry.h"
#include "viewer/utc.h"

using namespace viewer::catalog;

namespace {

// Two entries as the catalog writes them, trimmed, and some it should not take.
constexpr const char* kCatalog = R"([
    {"Name": "Teh's Trails", "Description": "Routes.", "Download": "https://mp-repo.blishhud.com/packs/TehsTrails.taco",
     "Info": "https://example.com/teh", "FileName": "TehsTrails.taco", "Categories": "General, Leveling", "Version": "1",
     "Size": 19.5, "TotalDownloads": 120, "AuthorName": "Teh", "AuthorUsername": "", "LastUpdate": "2026-06-16T14:38:50",
     "MapIds": [15, 17]},
    {"name": "lowercase keys", "download": "http://example.com/a.zip", "filename": "a.zip", "info": "not a link"},
    {"Name": "escapes", "Download": "https://example.com/x.zip", "FileName": "../x.zip"},
    {"Name": "not an archive", "Download": "https://example.com/x.exe", "FileName": "x.exe"},
    {"Name": "no address", "Download": "file:///C:/x.zip", "FileName": "y.zip"},
    "not an entry"
])";

Package package(std::string file_name, viewer::UtcMillis last_update = 1000) {
    Package out;
    out.name = file_name;
    out.file_name = std::move(file_name);
    out.download = "https://example.com/" + out.file_name;
    out.last_update = last_update;
    return out;
}

// The smallest pack in the corpus, to stand in for a download.
std::filesystem::path smallestPack() {
    std::filesystem::path smallest;
    uintmax_t least = UINTMAX_MAX;
    for (const std::filesystem::path& path : corpus::packs()) {
        std::error_code ec;
        const uintmax_t size = std::filesystem::file_size(path, ec);
        if (!ec && size < least) {
            least = size;
            smallest = path;
        }
    }
    return smallest;
}

// Runs `downloads` until nothing is running.
void waitFor(const Downloads& downloads, const std::string& file_name) {
    for (int i = 0; i < 500; ++i) {
        const auto status = downloads.status(file_name);
        if (status && !status->running) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FAIL("the download never finished");
}

} // namespace

TEST_CASE("catalog: the entries Pathing reads, with what is unsafe to write left out") {
    const auto packages = parse(kCatalog);
    REQUIRE(packages);
    REQUIRE(packages->size() == 2);

    const Package& teh = (*packages)[0];
    CHECK(teh.name == "Teh's Trails");
    CHECK(teh.file_name == "TehsTrails.taco");
    CHECK(teh.download == "https://mp-repo.blishhud.com/packs/TehsTrails.taco");
    CHECK(teh.info == "https://example.com/teh");
    CHECK(teh.categories == "General, Leveling");
    CHECK(teh.author == "Teh");
    CHECK(teh.size_mb == 19.5f);
    CHECK(teh.total_downloads == 120);
    CHECK(viewer::formatUtc(teh.last_update) == "2026-06-16T14:38:50");

    // Newtonsoft reads names ignoring case; an Info that is no link is no link.
    CHECK((*packages)[1].file_name == "a.zip");
    CHECK((*packages)[1].info.empty());

    CHECK_FALSE(parse("{}"));
    CHECK_FALSE(parse("not json"));
}

TEST_CASE("catalog: a file name is safe when it is a bare archive name") {
    CHECK(safeFileName("TehsTrails.taco"));
    CHECK(safeFileName("hero.blish.pack.zip"));
    CHECK(safeFileName("Czokalapik-s-Guides-for-GW2Taco.TACO"));
    CHECK_FALSE(safeFileName(""));
    CHECK_FALSE(safeFileName("../x.zip"));
    CHECK_FALSE(safeFileName("a/b.zip"));
    CHECK_FALSE(safeFileName("a\\b.zip"));
    CHECK_FALSE(safeFileName("C:x.zip"));
    CHECK_FALSE(safeFileName("x.exe"));
    CHECK_FALSE(safeFileName("x.zip."));
    CHECK_FALSE(safeFileName(".zip"));
    CHECK_FALSE(safeFileName("CON.zip"));
    CHECK_FALSE(safeFileName("com1.taco"));
    CHECK(safeFileName("console.zip"));
}

TEST_CASE("catalog: a package's local copy is the pack of its name") {
    const std::vector<LocalPack> locals = {
        {"packs/TehsTrails-HeroPoints.taco", 5},
        {"packs/tehstrails.zip", 2000},
    };
    const LocalPack* teh = findLocal(package("TehsTrails.taco"), locals);
    REQUIRE(teh);
    CHECK(teh->path == "packs/tehstrails.zip"); // not the Hero Points pack
    CHECK(findLocal(package("Other.taco"), locals) == nullptr);

    CHECK(state(package("TehsTrails.taco", 1000), teh) == State::Current);
    CHECK(state(package("TehsTrails.taco", 3000), teh) == State::Stale);
    CHECK(state(package("TehsTrails.taco", 1000), nullptr) == State::Absent);
}

TEST_CASE("catalog: local packs know when they were written") {
    const corpus::TempDir temp("catalog_locals");
    corpus::writeFile(temp / "ours/a.zip", "a");
    corpus::writeFile(temp / "ours/b.taco", "b");
    std::filesystem::create_directories(temp / "ours/folder");

    const viewer::UtcMillis before = viewer::utcNow() - 60'000;
    const std::vector<LocalPack> locals =
        localPacks({temp / "ours/a.zip", temp / "ours/b.taco", temp / "ours/folder", temp / "ours/gone.zip"});
    REQUIRE(locals.size() == 2);
    CHECK(locals[0].written > before);
    CHECK(locals[0].written < viewer::utcNow() + 60'000);
}

TEST_CASE("catalog: Keep updated is on unless turned off, and kept") {
    const corpus::TempDir temp("catalog_auto");
    AutoUpdate choices;
    CHECK(choices.enabled("Teh's Trails"));
    choices.set("Teh's Trails", false);
    choices.set("Hero", false);
    choices.set("Hero", true);
    REQUIRE(choices.save(temp / STATE_FILE_NAME));

    AutoUpdate loaded;
    loaded.load(temp / STATE_FILE_NAME);
    CHECK_FALSE(loaded.enabled("Teh's Trails"));
    CHECK(loaded.enabled("Hero"));

    loaded.load(temp / "missing.json");
    CHECK(loaded.enabled("Teh's Trails"));
}

TEST_CASE("utc: the catalog's and the timers' dates read and write back") {
    const auto time = viewer::parseUtc("2026-06-16T14:38:50");
    REQUIRE(time);
    CHECK(viewer::formatUtc(*time) == "2026-06-16T14:38:50");
    CHECK(viewer::parseUtc("2026-06-16T14:38:50.123Z") == time);
    CHECK_FALSE(viewer::parseUtc("2026-02-30T00:00:00"));
    CHECK_FALSE(viewer::parseUtc("2026-06-16T25:00:00"));
    CHECK_FALSE(viewer::parseUtc(""));
}

TEST_CASE("downloads: a pack arrives beside its target, ready to replace it") {
    const std::filesystem::path pack = smallestPack();
    if (pack.empty()) SKIP(corpus::missingMessage());
    const corpus::TempDir temp("catalog_download");

    int progress_calls = 0;
    Downloads downloads([&](const std::string& url,
                            const std::filesystem::path& file,
                            const http::Progress& progress,
                            const http::Cancel&) {
        CHECK(url == "https://example.com/Teh.taco");
        std::filesystem::copy_file(pack, file, std::filesystem::copy_options::overwrite_existing);
        progress(10, 20);
        ++progress_calls;
        return std::string();
    });
    downloads.start(package("Teh.taco"), temp.path());
    waitFor(downloads, "Teh.taco");

    const auto status = downloads.status("Teh.taco");
    REQUIRE(status);
    CHECK(status->error.empty());
    CHECK(progress_calls == 1);

    const std::vector<Downloads::Finished> finished = downloads.take();
    REQUIRE(finished.size() == 1);
    CHECK(finished[0].target == temp / "Teh.taco");
    CHECK(finished[0].part == temp / "Teh.taco.part");
    CHECK(std::filesystem::exists(finished[0].part));
    CHECK(downloads.take().empty()); // handed over once

    // The frame could not put it in place: it says why, and the part goes.
    downloads.fail(finished[0], "open in the editor");
    CHECK(downloads.status("Teh.taco")->error == "open in the editor");
    CHECK_FALSE(std::filesystem::exists(finished[0].part));
}

TEST_CASE("downloads: what fails, or is no pack, leaves nothing behind") {
    const corpus::TempDir temp("catalog_download_fail");

    SECTION("the fetch fails") {
        Downloads downloads(
            [](const std::string&, const std::filesystem::path& file, const http::Progress&, const http::Cancel&) {
                corpus::writeFile(file, "half");
                return std::string("the download broke off");
            });
        downloads.start(package("a.zip"), temp.path());
        waitFor(downloads, "a.zip");
        CHECK(downloads.status("a.zip")->error == "the download broke off");
        CHECK(downloads.take().empty());
        CHECK_FALSE(std::filesystem::exists(temp / "a.zip.part"));
    }

    SECTION("the body is not an archive") {
        Downloads downloads(
            [](const std::string&, const std::filesystem::path& file, const http::Progress&, const http::Cancel&) {
                corpus::writeFile(file, "<html>not found</html>");
                return std::string();
            });
        downloads.start(package("a.zip"), temp.path());
        waitFor(downloads, "a.zip");
        CHECK(downloads.status("a.zip")->error == "the download is not a marker pack");
        CHECK_FALSE(std::filesystem::exists(temp / "a.zip.part"));
    }

    SECTION("stopping cancels what is running") {
        Downloads downloads(
            [](const std::string&, const std::filesystem::path&, const http::Progress&, const http::Cancel& cancel) {
                while (!cancel.cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return std::string("cancelled");
            });
        downloads.start(package("a.zip"), temp.path());
        CHECK(downloads.status("a.zip")->running);
        downloads.stop();
        CHECK_FALSE(downloads.status("a.zip"));
    }

    CHECK_FALSE(Downloads().status("never.zip"));
}

// Hidden: it reaches the web, so ctest never runs it. Run it by hand after touching http.* or
// the catalog's reading: markus_viewer_test "[.web]".
TEST_CASE("web: the real catalog reads, and its smallest pack downloads and opens", "[.web]") {
    const http::Cancel cancel;
    const std::optional<std::string> body = http::get(REPO_URL, cancel, std::chrono::seconds(15));
    REQUIRE(body);
    const auto packages = parse(*body);
    REQUIRE(packages);
    REQUIRE_FALSE(packages->empty());

    const Package* smallest = &packages->front();
    for (const Package& package : *packages) {
        if (package.size_mb > 0.0f && package.size_mb < smallest->size_mb) smallest = &package;
    }
    const corpus::TempDir temp("catalog_web");
    Downloads downloads;
    downloads.start(*smallest, temp.path());
    for (int i = 0; i < 6000 && downloads.status(smallest->file_name)->running; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto status = downloads.status(smallest->file_name);
    REQUIRE(status);
    INFO(smallest->name << ": " << status->error);
    CHECK(status->error.empty());
    CHECK(downloads.take().size() == 1);
}
