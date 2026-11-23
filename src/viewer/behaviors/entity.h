#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "viewer/behaviors/cron.h"

// What one live marker or trail's behaviors remember from frame to frame: Pathing keeps it in
// the entity and in each behavior object, and here it is one plain struct beside the copy of
// the baked pathable.
namespace viewer::behaviors {

// Why a pathable was last filtered: the filter whose FilterReason a hidden marker shows.
enum class FilterKind : uint8_t {
    None,
    Festival,
    Mount,
    Profession,
    Race,
    Specialization,
    MapType,
    Schedule,
    Raid,
    Behavior,
    Achievement,
    Script
};

// BounceModifier's tween: up when focused, back down when not.
struct BounceState {
    enum class Phase : uint8_t { Resting, Rising, Settling };
    Phase phase = Phase::Resting;
    double start = 0.0; // game seconds
    float from = 0.0f; // the offset Settling starts from
};

struct EntityState {
    bool focused = false; // within triggerrange, and its category on
    bool filtered = false; // BehaviorFiltered: hidden by a filter
    FilterKind reason = FilterKind::None;
    bool inactive = false; // its category was off last frame: Pathing's _wasInactive

    // The player unhid it while hidden markers were shown faded: Pathing removes every filter
    // from the entity, and its script with them, since Script is a filter too.
    bool filters_cleared = false;

    // Interacted with while its achievement filter was on: AchievementFilter's _triggered,
    // hidden for as long as the entity lives, which is until the map is loaded again.
    bool achievement_triggered = false;

    bool once_done = false; // script-once has run
    // The `info` it put on screen, kept to take that one back: a script may change the text.
    std::optional<std::string> shown_info;
    ScheduleState schedule;
    BounceState bounce;
    double last_copy = -1.0e300; // game seconds of the last copy, for the auto-trigger debounce
};

} // namespace viewer::behaviors
