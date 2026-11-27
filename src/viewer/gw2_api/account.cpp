#include "viewer/gw2_api/account.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>

#include "http/http.h"
#include "util/json_file.h"
#include "viewer/behaviors/states.h"
#include "viewer/gw2_api/api_key.h"

namespace viewer::account {

namespace {

// FNV-1a, 64 bits, in hex: enough to tell keys apart, and nothing to ask the API with.
std::string fingerprint(std::string_view key) {
    uint64_t hash = 14695981039346656037ull;
    for (const char c : key) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    return std::format("{:016x}", hash);
}

} // namespace

bool Progress::achievementHidden(int id, int bit) const {
    const auto found = achievements.find(id);
    if (found == achievements.end()) return false;
    const Achievement& achievement = found->second;
    return achievement.done || std::find(achievement.bits.begin(), achievement.bits.end(), bit) != achievement.bits.end();
}

bool Progress::raidsComplete(const std::vector<std::string>& names) const {
    return raids && std::all_of(names.begin(), names.end(), [&](const std::string& name) { return raids->count(name) > 0; });
}

std::optional<std::unordered_map<int, Achievement>> achievementsFromApi(std::string_view json) {
    const nlohmann::json body = nlohmann::json::parse(json, nullptr, false);
    if (!body.is_array()) return std::nullopt;
    std::unordered_map<int, Achievement> out;
    for (const nlohmann::json& entry : body) {
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_integer()) continue;
        Achievement achievement;
        // `repeated` is there only for an achievement that can be done again, counting how
        // often it was: such a one is never done for Pathing, however often it was.
        achievement.done = entry.contains("done") && entry["done"].is_boolean() && entry["done"].get<bool>() && !entry.contains("repeated");
        if (entry.contains("bits") && entry["bits"].is_array()) {
            for (const nlohmann::json& bit : entry["bits"]) {
                if (bit.is_number_integer()) achievement.bits.push_back(bit.get<int>());
            }
        }
        out[entry["id"].get<int>()] = std::move(achievement);
    }
    return out;
}

std::optional<std::set<std::string>> raidsFromApi(std::string_view json) {
    const nlohmann::json body = nlohmann::json::parse(json, nullptr, false);
    if (!body.is_array()) return std::nullopt;
    std::set<std::string> out;
    for (const nlohmann::json& name : body) {
        if (!name.is_string()) continue;
        std::string lower = name.get<std::string>();
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        out.insert(std::move(lower));
    }
    return out;
}

nlohmann::json toCache(const Progress& progress, std::string_view key) {
    nlohmann::json achievements = nlohmann::json::object();
    for (const auto& [id, achievement] : progress.achievements) {
        achievements[std::to_string(id)] = {{"done", achievement.done}, {"bits", achievement.bits}};
    }
    nlohmann::json out = {{"key", fingerprint(key)}, {"achievements", std::move(achievements)}};
    if (progress.raids) {
        out["raids"] = *progress.raids;
        out["raids_at"] = progress.raids_at;
    }
    return out;
}

Progress fromCache(const nlohmann::json& cache, std::string_view key, UtcMillis now) {
    Progress progress;
    if (!cache.is_object() || !cache.contains("key") || cache["key"] != fingerprint(key)) return progress;

    if (cache.contains("achievements") && cache["achievements"].is_object()) {
        for (const auto& [id_text, entry] : cache["achievements"].items()) {
            int id = 0;
            const auto [end, error] = std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
            if (error != std::errc() || end != id_text.data() + id_text.size() || !entry.is_object()) continue;
            Achievement achievement;
            achievement.done = util::json_file::readBool(entry, "done", false);
            if (entry.contains("bits") && entry["bits"].is_array()) {
                for (const nlohmann::json& bit : entry["bits"]) {
                    if (bit.is_number_integer()) achievement.bits.push_back(bit.get<int>());
                }
            }
            progress.achievements[id] = std::move(achievement);
        }
    }

    // Last week's kills say nothing of this week's.
    const UtcMillis raids_at = cache.contains("raids_at") && cache["raids_at"].is_number_integer() ? cache["raids_at"].get<UtcMillis>() : 0;
    if (cache.contains("raids") && cache["raids"].is_array() && behaviors::nextWeeklyReset(raids_at) > now) {
        std::set<std::string> raids;
        for (const nlohmann::json& name : cache["raids"]) {
            if (name.is_string()) raids.insert(name.get<std::string>());
        }
        progress.raids = std::move(raids);
        progress.raids_at = raids_at;
    }
    return progress;
}

void Fetcher::open(const std::filesystem::path& cache_file, const std::string& first_key, UtcMillis now, util::JobQueue* queue) {
    file = cache_file;
    jobs = queue;
    key.clear();
    setKey(first_key, now);
}

void Fetcher::setKey(const std::string& new_key, UtcMillis now) {
    stop();
    request.reset();
    key = new_key;
    current_progress = Progress{};
    cached = {};
    last_asked.reset();
    if (key.empty() || file.empty() || !jobs) return;
    cached = jobs->ask([file = file, key = key, now] {
        const std::optional<nlohmann::json> cache = util::json_file::readObject(file);
        return cache ? fromCache(*cache, key, now) : Progress{};
    });
}

bool Fetcher::takeCache() {
    if (!cached.valid()) return true;
    if (!util::ready(cached)) return false;
    current_progress = util::take(cached).value_or(Progress{});
    return true;
}

void Fetcher::update(Clock::time_point now, UtcMillis utc_now) {
    if (!takeCache()) return;
    if (request && request->finished()) {
        if (std::optional<Progress> answer = request->take()) current_progress = std::move(*answer);
        request.reset();
    }
    if (current_progress.raids && behaviors::nextWeeklyReset(current_progress.raids_at) <= utc_now) current_progress.raids.reset();

    // INTERVAL_CHECKACHIEVEMENTS in both of Pathing's states: five minutes and 10 ms.
    constexpr auto kInterval = std::chrono::milliseconds(300010);
    if (key.empty() || request || (last_asked && now - *last_asked < kInterval)) return;
    last_asked = now;
    request = std::make_unique<http::Background<Progress>>();
    request->start([key = key, file = file, progress = current_progress](const http::Cancel& cancel) mutable -> std::optional<Progress> {
        const std::string header = api_key::authorization(key);
        const auto ask = [&](const char* url) -> std::optional<std::string> {
            std::optional<http::Reply> reply = http::request(url, header, cancel, std::chrono::seconds(15));
            if (reply && reply->status == 200) return std::move(reply->body);
            return std::nullopt;
        };

        // Each endpoint on its own: one the API does not answer keeps what was known. An answer
        // replaces the list whole, where Pathing's AchievementStates only adds and updates —
        // the same within a session, but a cache kept for weeks would otherwise keep what the
        // API no longer lists.
        bool answered = false;
        if (const std::optional<std::string> body = ask("https://api.guildwars2.com/v2/account/achievements")) {
            if (std::optional<std::unordered_map<int, Achievement>> achievements = achievementsFromApi(*body)) {
                progress.achievements = std::move(*achievements);
                answered = true;
            }
        }
        if (const std::optional<std::string> body = ask("https://api.guildwars2.com/v2/account/raids")) {
            if (std::optional<std::set<std::string>> raids = raidsFromApi(*body)) {
                progress.raids = std::move(*raids);
                progress.raids_at = utcNow();
                answered = true;
            }
        }
        if (!answered) return std::nullopt;
        // A cache that could not be written is asked of the API again next time.
        if (!file.empty()) static_cast<void>(util::json_file::write(file, toCache(progress, key)));
        return progress;
    });
}

void Fetcher::clearCache() {
    stop();
    request.reset();
    current_progress = Progress{};
    cached = {};
    last_asked.reset();
    if (!file.empty() && jobs) {
        jobs->post([file = file] {
            std::error_code ignored;
            std::filesystem::remove(file, ignored);
        });
    }
}

void Fetcher::stop() {
    if (request) request->stop();
}

} // namespace viewer::account
