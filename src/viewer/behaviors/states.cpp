#include "viewer/behaviors/states.h"

#include <chrono>
#include <fstream>
#include <string>

#include "util/json_file.h"

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
        const std::optional<UtcMillis> expiry = parseUtc(when);
        if (guid && expiry) until_time_[*guid] = std::max(until_time_[*guid], *expiry);
    }
}

void BehaviorStates::merge(const BehaviorStates& other) {
    for (const auto& [guid, expiry] : other.until_time_) {
        UtcMillis& mine = until_time_[guid];
        if (expiry > mine) {
            mine = expiry;
            dirty_ = true;
        }
    }
}

bool BehaviorStates::save(const std::filesystem::path& file) {
    std::string text;
    for (const auto& [guid, expiry] : until_time_) text += toString(guid) + ",t" + formatUtc(expiry) + "\n";
    if (!util::json_file::writeText(file, text)) return false;
    dirty_ = false;
    return true;
}

} // namespace viewer::behaviors
