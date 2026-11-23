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
#include "viewer/utc.h"

// What the GW2 API says of the player's account, asked with the key the options register:
// Pathing's AchievementStates and RaidStates, which the `achievementid` and `raid` filters and
// the menu read. Kept in `account.json` beside the settings, so that it applies from the first
// frame and while the API cannot be reached.
namespace viewer::account {

constexpr const char* CACHE_FILE_NAME = "account.json";

// One entry of /v2/account/achievements: Pathing's AchievementStatus.
struct Achievement {
    bool done = false; // done, and not a repeatable one: Done && Repeated == null
    std::vector<int> bits; // the parts done so far
};

struct Progress {
    std::unordered_map<int, Achievement> achievements;
    // This week's raid encounters killed, lowercase; nothing until the API has said, which
    // RaidFilter reads as none.
    std::optional<std::set<std::string>> raids;
    UtcMillis raids_at = 0; // when the API said so: the kills are void at the next weekly reset

    // AchievementStates.IsAchievementHidden: an achievement the API has not listed is shown;
    // one done is hidden whatever the bit; else only the bit done is. A bit of -1, which a
    // marker naming none has, is never done.
    bool achievementHidden(int id, int bit) const;

    // RaidStates.AreRaidsComplete: every one of `raids` is in this week's kills.
    bool raidsComplete(const std::vector<std::string>& raids) const;
};

// The body of /v2/account/achievements, by id. Nothing when it is not that.
std::optional<std::unordered_map<int, Achievement>> achievementsFromApi(std::string_view json);

// The body of /v2/account/raids, lowercase. Nothing when it is not that.
std::optional<std::set<std::string>> raidsFromApi(std::string_view json);

// The cache's contents for `progress`, asked with `key`. The key itself is not written, only
// a fingerprint of it, so that one account's progress is never read back for another's.
nlohmann::json toCache(const Progress& progress, std::string_view key);

// What `cache` holds for `key` at `now`: nothing of another key's, and no raids from before
// the last weekly reset.
Progress fromCache(const nlohmann::json& cache, std::string_view key, UtcMillis now);

// Asks the API with the player's key now and every five minutes after, on a thread of its own,
// as Pathing's states do on their cadence, and writes every answer to the cache. Until the
// first answer, and whenever the API cannot be reached, what the cache held stands. With no
// key it asks nothing and knows nothing.
class Fetcher
{
public:
    using Clock = std::chrono::steady_clock;

    // Where the cache is, empty for none, and the key to ask with: the cache is read now.
    void open(const std::filesystem::path& cache_file, const std::string& key, UtcMillis now);

    // A new key, or none: what the last one said is forgotten and the new one's cache read and
    // asked again at once, as SubtokenUpdated does in Pathing.
    void setKey(const std::string& key, UtcMillis now);

    // From the frame: takes an answer that has come in, lets this week's raids go at the
    // weekly reset, and starts the next ask when it is due.
    void update(Clock::time_point now, UtcMillis utc_now);

    // Forgets everything, in memory and on disk, and asks again at once.
    void clearCache();

    // Waits for an ask still in flight, which is cancelled first.
    void stop();

    const Progress& progress() const { return progress_; }

private:
    std::filesystem::path file_;
    std::string key_;
    Progress progress_;
    // The ask in flight: it works on a copy of progress_, and hands the whole of it back.
    std::unique_ptr<http::Background<Progress>> request_;
    std::optional<Clock::time_point> last_asked_; // nothing: ask on the next update
};

} // namespace viewer::account
