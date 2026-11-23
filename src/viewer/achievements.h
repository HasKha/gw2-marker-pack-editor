#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "http/background.h"

// What an achievement is, for the tooltip Pathing shows on an achievement marker's gear and
// over a category in the menu (AchievementTooltipView): its name, description and requirement
// from the public /v2/achievements, which needs no key. Asked for the first time one is shown
// and kept in `achievements.json` for good, as it hardly ever changes.
namespace viewer::achievements {

constexpr const char* CACHE_FILE_NAME = "achievements.json";

struct Details {
    std::string name;
    std::string description; // without the game's colour markup, as CleanMessage leaves it
    std::string requirement;
    // Each bit's text, empty for a bit that is an item, a skin or a mini rather than text:
    // what a marker naming that bit shows in place of the requirement.
    std::vector<std::string> bits;

    // The requirement, or the text of `bit` when it has one, as UpdateAchievementView picks.
    const std::string& requirementFor(int bit) const;
};

// The game's markup taken out of `text`: <c=@flavor>…</c> keeps what it colours, as
// CleanMessage does, and <br> is a line break.
std::string withoutMarkup(std::string_view text);

// The body of /v2/achievements?ids=…, by id. Nothing when it is not that.
std::optional<std::unordered_map<int, Details>> detailsFromApi(std::string_view json);

// The cache file's contents, and back: an id the API does not know is kept as null, so it is
// not asked for again.
using Known = std::unordered_map<int, std::optional<Details>>;
nlohmann::json toCache(const Known& known);
Known fromCache(const nlohmann::json& cache);

// The details of every achievement shown, from the cache or, the first time, from the API, a
// batch at a time on a thread of its own. Everything is called from the frame.
class Catalog
{
public:
    using Clock = std::chrono::steady_clock;

    // Where the cache is, empty for none: it is read now.
    void open(const std::filesystem::path& cache_file);

    // The details of `id`, or null while they are not known: asked for on the next update.
    // Null for good for an id the API does not know.
    const Details* find(int id);

    // Takes an answer that has come in, and asks for what was looked up and is not known.
    void update(Clock::time_point now);

    // Forgets everything, in memory and on disk: what is shown next is asked for again.
    void clear();

    // Waits for an ask still in flight, which is cancelled first.
    void stop();

private:
    std::filesystem::path file_;
    Known known_;
    std::set<int> wanted_; // looked up, not known, not yet asked for
    // The ask in flight, which works on a copy of known_ and hands the whole of it back.
    std::unique_ptr<http::Background<Known>> request_;
    std::set<int> asking_; // what it asks for, wanted again if it fails
    std::optional<Clock::time_point> failed_at_; // the last ask the API did not answer
};

} // namespace viewer::achievements
