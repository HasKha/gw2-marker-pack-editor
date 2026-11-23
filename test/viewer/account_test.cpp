// What the API's account endpoints say, read as Pathing's AchievementStates and RaidStates
// read them.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "corpus.h"
#include "util/json_file.h"
#include "viewer/account.h"

using viewer::account::Progress;

namespace {

constexpr viewer::UtcMillis kNoon = 1795348800000; // 2026-11-22T12:00:00Z, a Sunday
constexpr viewer::UtcMillis kHour = 3'600'000;

Progress someProgress() {
    Progress progress;
    progress.achievements[1] = {true, {}};
    progress.achievements[2] = {false, {0, 3}};
    progress.raids = std::set<std::string>{"vale_guardian"};
    progress.raids_at = kNoon;
    return progress;
}

} // namespace

TEST_CASE("the account's cache reads back what was written, for the same key only", "[account]") {
    const nlohmann::json cache = viewer::account::toCache(someProgress(), "KEY-A");
    CHECK(cache.dump().find("KEY-A") == std::string::npos); // a fingerprint, never the key

    const Progress same = viewer::account::fromCache(cache, "KEY-A", kNoon + kHour);
    REQUIRE(same.achievements.size() == 2);
    CHECK(same.achievements.at(1).done);
    CHECK(same.achievements.at(2).bits == std::vector<int>{0, 3});
    REQUIRE(same.raids);
    CHECK(same.raids->count("vale_guardian"));

    const Progress other = viewer::account::fromCache(cache, "KEY-B", kNoon + kHour);
    CHECK(other.achievements.empty());
    CHECK_FALSE(other.raids);
}

TEST_CASE("cached raids are last week's once the weekly reset has passed", "[account]") {
    const nlohmann::json cache = viewer::account::toCache(someProgress(), "KEY");
    // Monday 2026-11-23 07:30 UTC is the reset after Sunday noon.
    CHECK(viewer::account::fromCache(cache, "KEY", kNoon + 19 * kHour).raids);
    const Progress after = viewer::account::fromCache(cache, "KEY", kNoon + 20 * kHour);
    CHECK_FALSE(after.raids);
    CHECK(after.achievements.size() == 2); // achievements do not reset
}

TEST_CASE("the fetcher starts from the cache, and clearing it forgets the file too", "[account]") {
    const corpus::TempDir temp("account_cache");
    const std::filesystem::path file = temp / viewer::account::CACHE_FILE_NAME;
    REQUIRE(util::json_file::write(file, viewer::account::toCache(someProgress(), "KEY")));

    viewer::account::Fetcher fetcher;
    fetcher.open(file, "KEY", kNoon + kHour);
    CHECK(fetcher.progress().achievementHidden(1, -1));
    CHECK(fetcher.progress().raidsComplete({"vale_guardian"}));

    fetcher.setKey("OTHER", kNoon + kHour);
    CHECK(fetcher.progress().achievements.empty());
    fetcher.setKey("KEY", kNoon + kHour);
    CHECK(fetcher.progress().achievementHidden(1, -1));

    fetcher.clearCache();
    CHECK(fetcher.progress().achievements.empty());
    CHECK_FALSE(std::filesystem::exists(file));
}

TEST_CASE("account achievements are read with their bits, a repeatable one never done", "[account]") {
    const auto achievements = viewer::account::achievementsFromApi(R"([
        {"id": 1, "current": 5, "max": 5, "done": true},
        {"id": 2, "bits": [0, 3], "current": 2, "max": 5, "done": false},
        {"id": 3, "current": 1, "max": 1, "done": true, "repeated": 4},
        {"name": "no id"}
    ])");
    REQUIRE(achievements);
    REQUIRE(achievements->size() == 3);
    CHECK(achievements->at(1).done);
    CHECK(achievements->at(2).bits == std::vector<int>{0, 3});
    CHECK_FALSE(achievements->at(3).done);

    CHECK_FALSE(viewer::account::achievementsFromApi(R"({"text":"Invalid access token"})"));
}

TEST_CASE("an achievement hides what is done of it, and nothing the API has not listed", "[account]") {
    Progress progress;
    progress.achievements[1] = {true, {}};
    progress.achievements[2] = {false, {0, 3}};

    CHECK(progress.achievementHidden(1, -1));
    CHECK(progress.achievementHidden(1, 7)); // done: every bit with it
    CHECK(progress.achievementHidden(2, 3));
    CHECK_FALSE(progress.achievementHidden(2, 1));
    CHECK_FALSE(progress.achievementHidden(2, -1)); // no bit named: hidden only once done
    CHECK_FALSE(progress.achievementHidden(99, 0));
}

TEST_CASE("this week's raids are read lowercase, and complete only once all are", "[account]") {
    const auto raids = viewer::account::raidsFromApi(R"(["Vale_Guardian", "spirit_woods", 5])");
    REQUIRE(raids);
    CHECK(*raids == std::set<std::string>{"vale_guardian", "spirit_woods"});

    Progress progress;
    CHECK_FALSE(progress.raidsComplete({"vale_guardian"})); // nothing known yet
    progress.raids = raids;
    CHECK(progress.raidsComplete({"vale_guardian", "spirit_woods"}));
    CHECK_FALSE(progress.raidsComplete({"vale_guardian", "gorseval"}));
    CHECK_FALSE(progress.raidsComplete({""}));
}
