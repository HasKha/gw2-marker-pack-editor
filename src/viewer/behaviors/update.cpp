#include "viewer/behaviors/update.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

#include "viewer/behaviors/festivals.h"

namespace viewer::behaviors {

namespace {

using runtime::LiveMarker;
using runtime::LiveTrail;

// AdvancedDefaults.CopyAttributeRechargeMs: an auto-triggered copy waits this long between
// copies, so walking about inside a marker's range does not copy every frame.
constexpr double kCopyRechargeSeconds = 8.0;

constexpr const char* kUnhideSuffix = "\n\nPress {0} or click this gear to unhide the marker.";

// Glide's eases (Ease.cs), which BounceModifier picks from.
float quadInOut(float t) {
    return t <= 0.5f ? t * t * 2.0f : 1.0f - (t - 1.0f) * (t - 1.0f) * 2.0f;
}

float quartInOut(float t) {
    if (t <= 0.5f) return t * t * t * t * 8.0f;
    const float u = t * 2.0f - 2.0f;
    return (1.0f - u * u * u * u) / 2.0f + 0.5f;
}

float bounceOut(float t) {
    constexpr float b1 = 1.0f / 2.75f, b2 = 2.0f / 2.75f, b3 = 1.5f / 2.75f, b4 = 2.5f / 2.75f, b5 = 2.25f / 2.75f, b6 = 2.625f / 2.75f;
    if (t < b1) return 7.5625f * t * t;
    if (t < b2) return 7.5625f * (t - b3) * (t - b3) + 0.75f;
    if (t < b4) return 7.5625f * (t - b5) * (t - b5) + 0.9375f;
    return 7.5625f * (t - b6) * (t - b6) + 0.984375f;
}

// The share of `duration` gone since `start`, 0..1; all of it for a duration of nothing.
float progress(double now, double start, float duration) {
    if (duration <= 0.0f) return 1.0f;
    return static_cast<float>(std::clamp((now - start) / duration, 0.0, 1.0));
}

template <class T>
bool contains(const std::vector<T>& values, const T& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::string joined(const std::vector<int>& values, Gw2Enum type) {
    std::string out;
    for (int value : values) {
        if (!out.empty()) out += ", ";
        out += enumName(type, value);
    }
    return out;
}

// What the frame's player is, worked out once per frame rather than per marker.
struct Player {
    glm::vec3 position{0.0f}; // in the file's space, which markers are baked in
    Guid character; // the name's MD5, which OnceDailyPerCharacter keys by
    uint64_t instance = 0;
};

Player playerOf(const MumbleState& mumble) {
    Player player;
    // Blish's (x, z, y) back to the file's (x, y, z).
    player.position = glm::vec3(mumble.player_position.x, mumble.player_position.z, mumble.player_position.y);
    player.character = md5Guid(mumble.name);
    player.instance = instanceKey(mumble.map_id, mumble.shard_id);
    return player;
}

// The guid the `behavior` filter keys by.
Guid behaviorGuid(const Behaviors& behaviors, const BakedMarker& marker, const Player& player) {
    return behaviors.mode == static_cast<int>(Mode::OnceDailyPerCharacter) ? xorGuid(marker.guid, player.character) : marker.guid;
}

// What one frame needs while running a marker's behaviors.
class Runner
{
public:
    Runner(runtime::LiveMap* live, const Host& host, const Frame& frame)
        : live_(*live)
        , host_(host)
        , frame_(frame)
        , player_(playerOf(*frame.mumble)) {}

    // The filters, in StandardMarker_Behaviors' order, with the script's once and tick
    // between them and its own filter, as Script.Update runs before its IsFiltered.
    //
    // A script may create markers, which moves every live marker: nothing in the live map is
    // held by reference across a call into one.
    void filter(Entity entity) {
        const std::shared_ptr<const Behaviors> keep = behaviorsOf(entity);
        const Behaviors& b = *keep;
        bool filtered = false;
        FilterKind reason = FilterKind::None;
        const auto check = [&](bool hit, FilterKind kind) {
            if (!hit) return;
            filtered = true;
            reason = kind;
        };

        if (!stateOf(entity).filters_cleared) {
            const MumbleState& mumble = *frame_.mumble;
            if (b.festivals) {
                const bool on = std::any_of(b.festivals->begin(), b.festivals->end(), [&](const std::string& name) {
                    return frame_.festivals && contains(*frame_.festivals, name);
                });
                check(!on, FilterKind::Festival);
            }
            if (b.mounts) check(!contains(*b.mounts, mumble.mount), FilterKind::Mount);
            if (b.professions) check(!contains(*b.professions, mumble.profession), FilterKind::Profession);
            if (b.races) check(!contains(*b.races, mumble.race), FilterKind::Race);
            if (b.specializations) check(!contains(*b.specializations, mumble.specialization), FilterKind::Specialization);
            if (b.map_types) check(!contains(*b.map_types, mumble.map_type), FilterKind::MapType);
            if (b.schedule)
                check(updateSchedule(*b.schedule, b.schedule_duration, frame_.now, &stateOf(entity).schedule), FilterKind::Schedule);
            if (b.raids) check(!b.raids->empty() && !(frame_.account && frame_.account->raidsComplete(*b.raids)), FilterKind::Raid);
            if (b.mode > 0 && !entity.trail) {
                const BakedMarker& marker = live_.markers[entity.index].marker;
                const bool hidden =
                    host_.states->hidden(static_cast<Mode>(b.mode), behaviorGuid(b, marker, player_), player_.instance, frame_.now);
                check(b.invert ? !hidden : hidden, FilterKind::Behavior);
            }
            if (b.achievement) {
                const bool done = frame_.account && frame_.account->achievementHidden(b.achievement->id, b.achievement->bit);
                check(stateOf(entity).achievement_triggered || done, FilterKind::Achievement);
            }
            if (b.scripted && host_.script) {
                if (b.once && !stateOf(entity).once_done) host_.script(*b.once, entity, Hook::Once, false);
                stateOf(entity).once_done = true;
                if (b.tick) host_.script(*b.tick, entity, Hook::Tick, false);
                if (b.filter) check(host_.script(*b.filter, entity, Hook::Filter, false).value_or(false), FilterKind::Script);
            }
        }
        EntityState& state = stateOf(entity);
        state.filtered = host_.options.auto_hide && filtered;
        state.reason = state.filtered ? reason : FilterKind::None;
    }

    void focus(size_t index) {
        LiveMarker& live = live_.markers[index];
        EntityState& state = live.behavior;
        if (state.focused) return;
        state.focused = true;
        const std::shared_ptr<const Behaviors> keep = live.marker.behaviors;
        const Behaviors& b = *keep;

        // Each ICanFocus in turn; a later one's prompt replaces an earlier one's.
        if (b.mode > 0 && !state.filters_cleared) prompt(index, modePrompt(b, live.marker));
        // AchievementFilter's gear, whose tooltip in Pathing is the achievement's details.
        if (b.achievement && !state.filters_cleared) prompt(index, "Hide achievement marker until map change {0}", b.achievement->id);
        if (b.info && !state.filtered) {
            host_.screen->info.push_back(*b.info);
            state.shown_info = *b.info;
        }
        if (b.bounce && host_.options.animate) {
            state.bounce.phase = state.filtered ? BounceState::Phase::Resting : BounceState::Phase::Rising;
            state.bounce.start = frame_.seconds;
        }
        if (b.copy) prompt(index, "Copy '" + b.copy->value + "' to clipboard {0}");
        if (!b.toggle.empty()) prompt(index, "Toggle '" + b.toggle + "' category {0}");
        if (b.reset_guids) {
            const size_t count = b.reset_guids->size();
            prompt(index, "Will unhide " + std::to_string(count) + " marker" + (count == 1 ? "" : "s") + " {0}");
        }
        if (!b.show.empty()) prompt(index, "Show '" + b.show + "' category {0}");
        if (!b.hide.empty()) prompt(index, "Hide '" + b.hide + "' category {0}");
        if (scriptRuns(live)) {
            if (b.trigger) prompt(index, "Will trigger a script {0}");
            // The last to read `live`: the call may move it.
            if (b.focus) host_.script(*b.focus, Entity{false, index}, Hook::Focus, true);
        }

        if (b.auto_trigger) interact(index, true);

        if (live_.markers[index].behavior.filtered && host_.options.show_hidden) {
            host_.screen->prompt = Screen::Prompt{index, filterReason(live_.markers[index]) + kUnhideSuffix, true};
        }
    }

    void unfocus(size_t index) {
        LiveMarker& live = live_.markers[index];
        EntityState& state = live.behavior;
        if (state.focused) {
            state.focused = false;
            const std::shared_ptr<const Behaviors> keep = live.marker.behaviors;
            const Behaviors& b = *keep;
            takeInfoBack(&state);
            if (b.bounce) {
                // Whatever height it reached, it falls back from there.
                state.bounce.from = bounceOffset(live, frame_.seconds);
                state.bounce.phase = BounceState::Phase::Settling;
                state.bounce.start = frame_.seconds;
            }
            if (scriptRuns(live) && b.focus) host_.script(*b.focus, Entity{false, index}, Hook::Focus, false);
        }
        disconnect(index);
    }

    void interact(size_t index, bool auto_triggered) {
        LiveMarker& live = live_.markers[index];
        EntityState& state = live.behavior;
        const std::shared_ptr<const Behaviors> keep = live.marker.behaviors;
        const Behaviors& b = *keep;

        if (!auto_triggered && state.filtered && host_.options.show_hidden) {
            state.filters_cleared = true;
            state.filtered = false;
            state.reason = FilterKind::None;
            host_.states->clear(live.marker.guid);
            unfocus(index);
            return;
        }
        if (state.filtered) return;

        if (b.mode > 0 && !state.filters_cleared) {
            hideByMode(b, live.marker);
            disconnect(index);
        }
        if (b.achievement && !state.filters_cleared) {
            state.achievement_triggered = true;
            disconnect(index);
        }
        if (b.copy) copy(b.copy.value(), auto_triggered, &state);
        if (!b.toggle.empty()) setCategory(b.toggle, !categoryEnabled(b.toggle));
        if (b.reset_guids) {
            for (const Guid& guid : *b.reset_guids) host_.states->clear(guid);
        }
        if (!b.show.empty()) setCategory(b.show, true);
        if (!b.hide.empty()) setCategory(b.hide, false);
        if (scriptRuns(live) && b.trigger) host_.script(*b.trigger, Entity{false, index}, Hook::Trigger, auto_triggered);
    }

    // InfoModifier.InfoValue's setter: a script that changes the text of a focused marker's
    // info changes what is on screen.
    void refreshInfo(size_t index) {
        EntityState& state = live_.markers[index].behavior;
        const Behaviors& b = *live_.markers[index].marker.behaviors;
        if (!state.focused || !state.shown_info || (b.info && *b.info == *state.shown_info)) return;
        takeInfoBack(&state);
        if (b.info && !state.filtered) {
            host_.screen->info.push_back(*b.info);
            state.shown_info = *b.info;
        }
    }

    float distanceToPlayer(const BakedMarker& marker) const {
        return glm::distance(marker.position - glm::vec3(0.0f, marker.height_offset, 0.0f), player_.position);
    }

private:
    std::shared_ptr<const Behaviors> behaviorsOf(Entity entity) const {
        return entity.trail ? live_.trails[entity.index].trail.behaviors : live_.markers[entity.index].marker.behaviors;
    }

    EntityState& stateOf(Entity entity) {
        return entity.trail ? live_.trails[entity.index].behavior : live_.markers[entity.index].behavior;
    }

    bool scriptRuns(const LiveMarker& live) const {
        return host_.script && live.marker.behaviors->scripted && !live.behavior.filters_cleared;
    }

    // SmallInteract.ShowInteract: not for a hidden marker, whose own prompt is the unhide one.
    void prompt(size_t index, std::string text, std::optional<int> achievement = std::nullopt) {
        if (live_.markers[index].behavior.filtered) return;
        host_.screen->prompt = Screen::Prompt{index, std::move(text), false, achievement};
    }

    void takeInfoBack(EntityState* state) {
        if (!state->shown_info) return;
        std::vector<std::string>& info = host_.screen->info;
        if (const auto at = std::find(info.begin(), info.end(), *state->shown_info); at != info.end()) info.erase(at);
        state->shown_info.reset();
    }

    void disconnect(size_t index) {
        if (host_.screen->prompt && host_.screen->prompt->marker == index) host_.screen->prompt.reset();
    }

    std::string modePrompt(const Behaviors& b, const BakedMarker& marker) const {
        switch (static_cast<Mode>(b.mode)) {
        case Mode::ReappearOnMapChange: return "Hide marker until map change {0}";
        case Mode::ReappearOnDailyReset:
            return "Hide marker until daily reset (" + humanize((nextDailyReset(frame_.now) - frame_.now) / 1000.0, 2) + ") {0}";
        case Mode::OnlyVisibleBeforeActivation: return "Hide marker permanently {0}";
        case Mode::ReappearAfterTimer: return "Hide marker for " + humanize(marker.reset_length, 4) + " {0}";
        case Mode::ReappearOnMapReset: return "Hide marker until map reset {0}";
        case Mode::OncePerInstance: return "Hide marker until you join a new instance of this map {0}";
        case Mode::OnceDailyPerCharacter: return "Hide marker for this character until daily reset {0}";
        case Mode::ReappearOnWeeklyReset: return "Hide marker until weekly reset {0}";
        default: return "Interact with behavior {0}";
        }
    }

    void hideByMode(const Behaviors& b, const BakedMarker& marker) {
        BehaviorStates& states = *host_.states;
        const Mode mode = static_cast<Mode>(b.mode);
        switch (mode) {
        case Mode::ReappearOnMapChange:
        case Mode::OnlyVisibleBeforeActivation:
        case Mode::OncePerInstance: states.hide(mode, marker.guid, player_.instance); break;
        case Mode::ReappearOnDailyReset: states.hideUntil(marker.guid, nextDailyReset(frame_.now)); break;
        case Mode::ReappearAfterTimer:
            states.hideUntil(marker.guid, frame_.now + static_cast<UtcMillis>(static_cast<double>(marker.reset_length) * 1000.0));
            break;
        case Mode::OnceDailyPerCharacter: states.hideUntil(xorGuid(marker.guid, player_.character), nextDailyReset(frame_.now)); break;
        case Mode::ReappearOnWeeklyReset: states.hideUntil(marker.guid, nextWeeklyReset(frame_.now)); break;
        default: break;
        }
    }

    // CopyModifier.Interact.
    void copy(const Copy& copy, bool auto_triggered, EntityState* state) {
        const ClipboardConsent consent = host_.options.clipboard;
        if (consent == ClipboardConsent::Never || (consent == ClipboardConsent::OnlyWhenInteractedWith && auto_triggered)) return;
        if (auto_triggered && frame_.seconds - state->last_copy < kCopyRechargeSeconds) return;
        state->last_copy = frame_.seconds;
        if (copy.value.empty() || !host_.copy) return;
        host_.copy(copy.value, formatMessage(copy.message, copy.value));
    }

    bool categoryDefault(const std::string& id) const {
        const size_t node = host_.tree ? host_.tree->find(id) : pack::kNoIndex;
        return node == pack::kNoIndex || host_.tree->nodes[node].default_toggle;
    }

    bool categoryEnabled(const std::string& id) const { return host_.categories->enabled(id, categoryDefault(id)); }

    void setCategory(const std::string& id, bool enabled) { host_.categories->set(id, enabled, categoryDefault(id)); }

    runtime::LiveMap& live_;
    const Host& host_;
    const Frame& frame_;
    Player player_;
};

} // namespace

void update(runtime::LiveMap* live, const std::vector<char>& visible, const Host& host, const Frame& frame) {
    Runner runner(live, host, frame);

    // By index throughout, and the size read every time: scripts called from here may add
    // markers, which run this same frame.
    for (size_t i = 0; i < live->markers.size(); ++i) {
        if (!live->markers[i].marker.behaviors) continue;

        // A category turned off, or a marker a script removed, loses focus on the way out.
        if (live->markers[i].removed || !runtime::nodeVisible(live->markers[i].node, visible)) {
            if (!live->markers[i].behavior.inactive) runner.unfocus(i);
            live->markers[i].behavior.inactive = true;
            continue;
        }
        live->markers[i].behavior.inactive = false;

        runner.filter(Entity{false, i});
        const LiveMarker& marker = live->markers[i];
        if (runner.distanceToPlayer(marker.marker) <= marker.marker.trigger_range) {
            runner.focus(i);
        } else {
            runner.unfocus(i);
        }
        runner.refreshInfo(i);
    }

    for (size_t i = 0; i < live->trails.size(); ++i) {
        const LiveTrail& trail = live->trails[i];
        if (!trail.trail.behaviors || trail.removed || !runtime::nodeVisible(trail.node, visible)) continue;
        runner.filter(Entity{true, i});
    }
}

void interact(runtime::LiveMap* live, const Host& host, const Frame& frame) {
    Runner runner(live, host, frame);
    for (size_t i = 0; i < live->markers.size(); ++i) {
        if (live->markers[i].behavior.focused) runner.interact(i, false);
    }
}

void interactWith(runtime::LiveMap* live, size_t marker, const Host& host, const Frame& frame) {
    if (marker >= live->markers.size() || !live->markers[marker].marker.behaviors) return;
    Runner runner(live, host, frame);
    runner.interact(marker, false);
}

float bounceOffset(const runtime::LiveMarker& marker, double seconds) {
    const BounceState& state = marker.behavior.bounce;
    if (state.phase == BounceState::Phase::Resting || !marker.marker.behaviors || !marker.marker.behaviors->bounce) return 0.0f;
    const Bounce& bounce = *marker.marker.behaviors->bounce;

    if (state.phase == BounceState::Phase::Settling)
        return state.from * (1.0f - bounceOut(progress(seconds, state.start, bounce.duration)));

    const double elapsed = seconds - state.start - bounce.delay;
    if (elapsed <= 0.0) return 0.0f;
    if (bounce.mode == BounceMode::Rise || bounce.duration <= 0.0f)
        return bounce.height * quartInOut(progress(elapsed, 0.0, bounce.duration));

    // Repeat().Reflect(): up over one duration, down over the next, for as long as it lasts.
    const double cycles = elapsed / bounce.duration;
    const float t = static_cast<float>(cycles - std::floor(cycles));
    const bool returning = static_cast<int64_t>(cycles) % 2 == 1;
    const float eased = quadInOut(t);
    return bounce.height * (returning ? 1.0f - eased : eased);
}

std::string filterReason(const runtime::LiveMarker& marker) {
    const Behaviors* b = marker.marker.behaviors.get();
    if (!b) return {};
    switch (marker.behavior.reason) {
    case FilterKind::Festival: {
        std::string names;
        for (const std::string& name : *b->festivals) names += (names.empty() ? "" : ", ") + std::string(festivalDisplayName(name));
        return "Hidden because the " + names + " festival isn't active.";
    }
    case FilterKind::Mount: return "Hidden because you're not on the mount: " + joined(*b->mounts, Gw2Enum::Mount) + ".";
    case FilterKind::Profession: return "Hidden because you're not the profession: " + joined(*b->professions, Gw2Enum::Profession) + ".";
    case FilterKind::Race: return "Hidden because you're not: " + joined(*b->races, Gw2Enum::Race) + ".";
    case FilterKind::Specialization: return "Hidden because you're not the correct specialization.";
    case FilterKind::MapType: return "Hidden because you're not on the map type: " + joined(*b->map_types, Gw2Enum::MapType) + ".";
    case FilterKind::Schedule: return "Hidden because it is only scheduled to show at certain times of day.";
    case FilterKind::Raid: {
        // Pathing's says "because you've completed", the opposite of what its filter does.
        std::string names;
        for (const std::string& name : *b->raids) names += (names.empty() ? "" : ", ") + name;
        return "Hidden until you've completed this week: " + names + ".";
    }
    case FilterKind::Script: return "Hidden by a script.";
    case FilterKind::Achievement: return "Hidden because you've completed the achievement associated with this marker.";
    case FilterKind::Behavior:
        switch (static_cast<Mode>(b->mode)) {
        case Mode::ReappearOnMapChange: return "Hidden until the next map change because you interacted with it.";
        case Mode::ReappearOnDailyReset: return "Hidden until daily reset because you interacted with it.";
        case Mode::OnlyVisibleBeforeActivation: return "Hidden permanently because you interacted with it.";
        case Mode::ReappearAfterTimer: return "Hidden for a period of time because you interacted with it.";
        case Mode::ReappearOnMapReset: return "Hidden until map reset because you interacted with it.";
        case Mode::OncePerInstance: return "Hidden until you change map instances because you interacted with it.";
        case Mode::OnceDailyPerCharacter: return "Hidden for this character until daily reset because you interacted with it.";
        case Mode::ReappearOnWeeklyReset: return "Hidden until weekly reset because you interacted with it.";
        default: return "Unknown";
        }
    case FilterKind::None: break;
    }
    return {};
}

std::string humanize(double seconds, int precision) {
    struct Unit {
        const char* name;
        int64_t seconds;
    };
    static constexpr Unit kUnits[] = {{"week", 604800}, {"day", 86400}, {"hour", 3600}, {"minute", 60}, {"second", 1}};
    int64_t left = static_cast<int64_t>(seconds);
    std::string out;
    int used = 0;
    for (const Unit& unit : kUnits) {
        const int64_t count = left / unit.seconds;
        left %= unit.seconds;
        if (count == 0 || used == precision) continue;
        if (!out.empty()) out += ", ";
        out += std::to_string(count) + " " + unit.name + (count == 1 ? "" : "s");
        ++used;
    }
    return out.empty() ? "no time" : out;
}

} // namespace viewer::behaviors
