#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "viewer/utc.h"

// The `schedule` filter's cron expressions, read as Pathing reads them: with Cronos, five
// fields, or six with seconds first (AttributeParsingUtil.GetValueAsCronExpression), in UTC.
namespace viewer::behaviors {

class CronSchedule
{
public:
    // Numbers, `*`, `?`, ranges `a-b`, steps `/n`, lists `a,b`, month and weekday names, and
    // 7 for Sunday as well as 0. Cronos' `L`, `W` and `#` are not read: no pack uses them, and
    // an expression with one is no schedule, as an unparsable one is to Pathing.
    static std::optional<CronSchedule> parse(std::string_view text);

    // The first whole second at or after `from` the expression matches, or nothing when none
    // comes in the next 28 years — a cycle of weekdays and leap years, so an expression that
    // never matches, such as 30 February, is told from a rare one.
    std::optional<UtcMillis> next(UtcMillis from) const;

private:
    uint64_t seconds_ = 0; // bit per value
    uint64_t minutes_ = 0;
    uint32_t hours_ = 0;
    uint32_t days_ = 0; // of the month, bits 1..31
    uint16_t months_ = 0; // bits 1..12
    uint8_t weekdays_ = 0; // bits 0..6, Sunday first

    // The time of day, in seconds, of the first match at or after `start` seconds into a day.
    std::optional<int> firstTimeOfDay(int start) const;
};

// ScheduleFilter's state machine, Update and constructor alike: hidden except from each
// occurrence for `duration`. `started` is false until the first update, which looks back one
// duration, as the constructor does, so a window already open when the map loads shows.
struct ScheduleState {
    bool started = false;
    std::optional<UtcMillis> next_trigger; // DateTime.MinValue while unknown
    bool filtered = false;
};

// Advances `state` to `now` and says whether the pathable is filtered.
bool updateSchedule(const CronSchedule& schedule, UtcMillis duration, UtcMillis now, ScheduleState* state);

} // namespace viewer::behaviors
