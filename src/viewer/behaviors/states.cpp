#include "viewer/behaviors/states.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

namespace viewer::behaviors {

namespace {

using namespace std::chrono;

// DateTime.MaxValue to the second, which OnlyVisibleBeforeActivation hides until: so that
// "forever" is written as Pathing writes it, and reads back as the same moment.
constexpr UtcMillis kForever = 253402300799000;

constexpr UtcMillis kMillisPerDay = 24 * 60 * 60 * 1000;
constexpr UtcMillis kWeeklyResetTimeOfDay = (7 * 60 + 30) * 60 * 1000;

UtcMillis startOfDay(UtcMillis now) {
    const sys_days day = floor<days>(sys_time<milliseconds>{milliseconds{now}});
    return duration_cast<milliseconds>(day.time_since_epoch()).count();
}

// "yyyy-MM-ddTHH:mm:ss", the "s" format.
std::string formatTime(UtcMillis time) {
    const sys_time<milliseconds> point{milliseconds{time}};
    const sys_days day = floor<days>(point);
    const year_month_day date{day};
    const hh_mm_ss<seconds> clock{floor<seconds>(point - day)};
    char text[32];
    std::snprintf(
        text,
        sizeof(text),
        "%04d-%02u-%02uT%02d:%02d:%02d",
        static_cast<int>(date.year()),
        static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day()),
        static_cast<int>(clock.hours().count()),
        static_cast<int>(clock.minutes().count()),
        static_cast<int>(clock.seconds().count()));
    return text;
}

std::optional<UtcMillis> parseTime(const std::string& text) {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(text.c_str(), "%d-%u-%uT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) return std::nullopt;
    const year_month_day date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
    if (!date.ok()) return std::nullopt;
    const auto time = sys_days{date} + hours{hour} + minutes{minute} + seconds{second};
    return duration_cast<milliseconds>(time.time_since_epoch()).count();
}

} // namespace

uint64_t instanceKey(int map_id, uint32_t shard_id) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(map_id)) << 32) + shard_id;
}

UtcMillis nextDailyReset(UtcMillis now) {
    return startOfDay(now) + kMillisPerDay;
}

UtcMillis nextWeeklyReset(UtcMillis now) {
    const sys_days today = floor<days>(sys_time<milliseconds>{milliseconds{now}});
    const int day_of_week = static_cast<int>(std::chrono::weekday{today}.c_encoding());
    int days_until_monday = (1 - day_of_week + 7) % 7;
    const UtcMillis midnight = startOfDay(now);
    if (days_until_monday == 0 && now - midnight > kWeeklyResetTimeOfDay) days_until_monday = 7;
    return midnight + days_until_monday * kMillisPerDay + kWeeklyResetTimeOfDay;
}

bool BehaviorStates::hidden(Mode mode, const Guid& guid, uint64_t instance, UtcMillis now) const {
    switch (mode) {
    case Mode::ReappearOnMapChange: return until_map_change_.contains(guid);
    case Mode::ReappearOnDailyReset:
    case Mode::OnlyVisibleBeforeActivation:
    case Mode::ReappearAfterTimer:
    case Mode::OnceDailyPerCharacter:
    case Mode::ReappearOnWeeklyReset: {
        const auto timer = until_time_.find(guid);
        return timer != until_time_.end() && timer->second > now;
    }
    case Mode::OncePerInstance: {
        const auto shard = in_instance_.find(instance);
        return shard != in_instance_.end() && shard->second.contains(guid);
    }
    default: return false;
    }
}

void BehaviorStates::hide(Mode mode, const Guid& guid, uint64_t instance) {
    switch (mode) {
    case Mode::ReappearOnMapChange: until_map_change_.insert(guid); break;
    case Mode::OnlyVisibleBeforeActivation: hideUntil(guid, kForever); break;
    case Mode::OncePerInstance: in_instance_[instance].insert(guid); break;
    default: break; // a timed mode hides through hideUntil
    }
}

void BehaviorStates::hideUntil(const Guid& guid, UtcMillis expiry) {
    UtcMillis& stored = until_time_[guid];
    stored = std::max(stored, expiry);
    dirty_ = true;
}

void BehaviorStates::clear(const Guid& guid) {
    // As ClearHiddenBehavior: the first place that held it.
    if (until_map_change_.erase(guid)) return;
    if (until_time_.erase(guid)) {
        dirty_ = true;
        return;
    }
    for (auto& [instance, guids] : in_instance_) {
        if (guids.erase(guid)) return;
    }
}

void BehaviorStates::expire(UtcMillis now) {
    std::erase_if(until_time_, [&](const auto& timer) {
        const bool expired = timer.second <= now;
        dirty_ |= expired;
        return expired;
    });
}

void BehaviorStates::mapChanged() {
    until_map_change_.clear();
}

void BehaviorStates::load(const std::filesystem::path& file) {
    until_time_.clear();
    dirty_ = false;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t comma = line.find(',');
        if (comma == std::string::npos) continue;
        const std::optional<Guid> guid = guidFromString(std::string_view(line).substr(0, comma));
        // Pathing before 1.1.5 wrote the date without its "t"; DateTime.Parse took either.
        std::string when = line.substr(comma + 1);
        if (!when.empty() && when.front() == 't') when.erase(0, 1);
        const std::optional<UtcMillis> expiry = parseTime(when);
        if (guid && expiry) until_time_[*guid] = std::max(until_time_[*guid], *expiry);
    }
}

bool BehaviorStates::save(const std::filesystem::path& file) {
    std::ofstream out(file, std::ios::trunc);
    if (!out) return false;
    for (const auto& [guid, expiry] : until_time_) out << toString(guid) << ",t" << formatTime(expiry) << '\n';
    if (!out) return false;
    dirty_ = false;
    return true;
}

} // namespace viewer::behaviors
