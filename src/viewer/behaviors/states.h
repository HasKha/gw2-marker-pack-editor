#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include "viewer/behaviors/cron.h"
#include "viewer/guid.h"

// Which markers the `behavior` attribute has hidden, and until when: Pathing's
// State/BehaviorStates.cs.
namespace viewer::behaviors {

// The `behavior` attribute's values (Behavior/StandardPathableBehavior.cs): TacO's, and one
// of Blish HUD's.
enum class Mode : int {
    AlwaysVisible = 0,
    ReappearOnMapChange = 1,
    ReappearOnDailyReset = 2,
    OnlyVisibleBeforeActivation = 3,
    ReappearAfterTimer = 4,
    ReappearOnMapReset = 5, // implemented by neither TacO nor Pathing: never hides
    OncePerInstance = 6,
    OnceDailyPerCharacter = 7,
    ReappearOnWeeklyReset = 101,
};

// Pathing's own name for the file, and its layout: a line per timer, "<guid>,t<expiry>", the
// guid in its "D" form and the expiry UTC to the second.
constexpr const char* TIMERS_FILE_NAME = "timers.txt";

// A map instance: the map and MumbleLink's shard, as GetMapInstanceKey packs them.
uint64_t instanceKey(int map_id, uint32_t shard_id);

// The next daily reset after `now`: midnight UTC.
UtcMillis nextDailyReset(UtcMillis now);

// The weekly reset ReappearOnWeeklyReset waits for: the coming Monday at 07:30 UTC — today's,
// when today is a Monday not yet past 07:30, as Pathing reckons it.
UtcMillis nextWeeklyReset(UtcMillis now);

class BehaviorStates
{
public:
    // Whether `guid` is hidden under `mode`. Every timed mode shares one set, as Pathing's do,
    // so a marker hidden by one is hidden whatever mode asks. A timer counts until it
    // expires; Pathing sweeps expired ones every five seconds and counts them until then.
    bool hidden(Mode mode, const Guid& guid, uint64_t instance, UtcMillis now) const;

    // Hides `guid` for a mode that waits for an event rather than a time: a map change, an
    // instance change, or forever.
    void hide(Mode mode, const Guid& guid, uint64_t instance);

    // Hides `guid` until `expiry`; a later expiry wins over an earlier one.
    void hideUntil(const Guid& guid, UtcMillis expiry);

    // Shows `guid` again, whatever hid it: a `resetguid` marker, or the player unhiding a
    // ghost.
    void clear(const Guid& guid);

    // Forgets timers that have run out, so the file does not keep them: UpdateTimers.
    void expire(UtcMillis now);

    // What ReappearOnMapChange waits for.
    void mapChanged();

    // Whether the timers changed since the last load or save; only they are written.
    bool dirty() const { return dirty_; }

    // A missing file leaves nothing hidden; a line that does not parse is skipped.
    void load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file);

private:
    std::unordered_set<Guid, GuidHash> until_map_change_;
    std::unordered_map<Guid, UtcMillis, GuidHash> until_time_;
    std::unordered_map<uint64_t, std::unordered_set<Guid, GuidHash>> in_instance_;
    bool dirty_ = false;
};

} // namespace viewer::behaviors
