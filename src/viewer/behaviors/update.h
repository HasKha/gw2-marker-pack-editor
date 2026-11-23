#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "viewer/behaviors/attributes.h"
#include "viewer/behaviors/states.h"
#include "viewer/mumble.h"
#include "viewer/registry.h"
#include "viewer/runtime.h"
#include "viewer/store.h"

// Behaviors run: every frame's filters, focus as the player walks into a marker's range,
// interaction with what is focused — Pathing's PathingEntity.Update, StandardMarker_Focus.cs
// and the behaviors under Behavior/, as functions over the live map.
namespace viewer::behaviors {

// ModuleSettings' MarkerClipboardConsentLevel.
enum class ClipboardConsent : int { Always = 0, OnlyWhenInteractedWith = 1, Never = 2 };

// The player's say over behaviors: Pathing's pack settings, with its defaults.
struct Options {
    bool auto_hide = true; // PackAllowMarkersToAutomaticallyHide: off shows every filtered marker
    bool animate = true; // PackAllowMarkersToAnimate: bounce
    bool show_hidden = false; // PackShowHiddenMarkersReducedOpacity: hidden markers faded, and unhidable
    ClipboardConsent clipboard = ClipboardConsent::Always;
};

// Calls a script-* function and says what it returned, as a bool: nothing when it returned
// nothing, did not exist, or failed.
using ScriptCaller = std::function<std::optional<bool>(const ScriptCall& call, Entity entity, Hook hook, bool flag)>;

// What behaviors put on screen: Pathing's UiStates. Info strings stack, the last one shown;
// one marker at a time has the interact prompt.
struct Screen {
    struct Prompt {
        size_t marker = 0;
        std::string text; // with "{0}" where the interact key's name goes
        bool unhides = false; // the prompt of a hidden marker, which interacting unhides
    };

    std::vector<std::string> info;
    std::optional<Prompt> prompt;
};

// Everything behaviors act on. Owned by whoever owns the live map.
struct Host {
    BehaviorStates* states = nullptr;
    store::CategoryStates* categories = nullptr;
    const registry::CategoryTree* tree = nullptr;
    Screen* screen = nullptr;

    // Copies `text`, telling the player with `message`. Null copies nothing.
    std::function<bool(const std::string& text, const std::string& message)> copy;

    // Null while scripts do not run: off, or the map's packs still loading, as Script.Update
    // waits for PackInitiator.
    ScriptCaller script;

    Options options;
};

// This frame.
struct Frame {
    const MumbleState* mumble = nullptr;
    UtcMillis now = 0; // wall clock, for timers and schedules
    double seconds = 0.0; // game time, for animation and the copy debounce
    const std::vector<std::string>* festivals = nullptr; // on today, lowercase; null for none
};

// One frame of every live marker and trail under a category `visible` shows, as
// runtime::visibleNodes gives it: filters, scripts' once and tick, and focus.
void update(runtime::LiveMap* live, const std::vector<char>& visible, const Host& host, const Frame& frame);

// The interact key: every focused marker is interacted with, as SharedPackState's
// OnInteractPressed does.
void interact(runtime::LiveMap* live, const Host& host, const Frame& frame);

// The prompt clicked: Pathing's gear.
void interactWith(runtime::LiveMap* live, size_t marker, const Host& host, const Frame& frame);

// How far bounce has lifted `marker` above where it stands, in metres.
float bounceOffset(const runtime::LiveMarker& marker, double seconds);

// What a hidden marker says it is hidden by: the FilterReason of `marker`'s filter.
std::string filterReason(const runtime::LiveMarker& marker);

// TimeSpan.Humanize(precision): "1 day, 2 hours", the largest units that are not zero.
std::string humanize(double seconds, int precision);

} // namespace viewer::behaviors
