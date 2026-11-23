#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "pack/model.h"
#include "viewer/behaviors/cron.h"
#include "viewer/guid.h"

// What a marker or trail does beyond being drawn, read from its attributes once, at the bake:
// the filters and modifiers of Pathing's Behavior/, in the order StandardMarker_Behaviors.cs
// registers them, which is the order they run in.
namespace viewer::behaviors {

// An argument a script-* attribute hands its function: Script.GetValueFromString's nil, bool,
// Int32, float, or a string with its quotes trimmed.
using ScriptArg = std::variant<std::monostate, bool, int, float, std::string>;

// "Name(arg, ...)" as Script.SplitFunc splits it — at the first "(" and at every comma, a
// quoted string's own commas included, as Pathing's comment there admits.
struct ScriptCall {
    std::string name;
    std::vector<ScriptArg> args;
};

// Nothing for a blank value.
std::optional<ScriptCall> parseScriptCall(std::string_view text);

// Which function of a script-* behavior is being called, and so what it is handed after the
// entity: Focus and Trigger a bool, Tick the GameTime, Filter and Once nothing.
enum class Hook { Tick, Focus, Trigger, Filter, Once };

struct Entity {
    bool trail = false;
    size_t index = 0; // into LiveMap::markers or ::trails
};

// string.Format(message, value) as ClipboardHelper.FormatMessage uses it: {0} is the value,
// {{ and }} are braces, and a message .NET would refuse to format is shown as it is.
std::string formatMessage(std::string_view message, std::string_view value);

// The text CopyModifier, and User.SetClipboard without a message, tell the player with.
constexpr const char* kDefaultCopyMessage = "'{0}' copied to clipboard.";

enum class BounceMode { Bounce, Rise };

// BounceModifier: rises by `height` metres when focused, after `delay` seconds, over
// `duration`; Bounce keeps going up and down, Rise stays up.
struct Bounce {
    BounceMode mode = BounceMode::Bounce;
    float delay = 0.0f;
    float height = 2.0f;
    float duration = 1.0f;
};

struct Copy {
    std::string value;
    std::string message = kDefaultCopyMessage;
};

struct Behaviors {
    // ── Filters, each there when its attribute is. A list that names nothing the game can
    // be still filters, always: All() over values none of which is the current one.
    std::optional<std::vector<std::string>> festivals; // Blish HUD's names, lowercase
    std::optional<std::vector<int>> mounts; // Gw2Enum::Mount
    std::optional<std::vector<int>> professions; // Gw2Enum::Profession
    std::optional<std::vector<int>> races; // Gw2Enum::Race
    std::optional<std::vector<int>> specializations;
    std::optional<std::vector<int>> map_types; // Gw2Enum::MapType
    std::optional<CronSchedule> schedule; // with a positive `schedule-duration`, or none
    UtcMillis schedule_duration = 0;
    // `raid`, the encounters' API names, lowercase. RaidFilter shows the marker only once all
    // are in /v2/account/raids, this week's kills, and hides it while it knows none: with no
    // API key registered, always.
    std::optional<std::vector<std::string>> raids;

    // `behavior`, a Mode; 0 and below is no filter. A value outside the enum filters nothing
    // but still says so on focus, as Pathing's does.
    int mode = 0;
    bool invert = false; // `invertbehavior`: shown only once hidden

    // AchievementFilter, there when `achievementid` or `achievementbit` is: hidden once the
    // account has done the achievement, or this bit of it (account::Progress), and for the
    // rest of the map once interacted with. An id that will not parse is 0, which no
    // achievement has; a bit that will not parse is 0, and none at all is -1, never done.
    struct Achievement {
        int id = 0;
        int bit = -1;
    };
    std::optional<Achievement> achievement;

    // ── Modifiers (markers only).
    bool auto_trigger = false; // interact on focus, without the key
    std::optional<std::string> info;
    std::optional<Bounce> bounce;
    std::optional<Copy> copy;
    std::string toggle; // a category id, lowercase; empty for none
    std::string show;
    std::string hide;
    std::optional<std::vector<Guid>> reset_guids;

    // ── Script. Focus and trigger are a marker's only: a trail is never focused.
    bool scripted = false; // any script-* attribute, which is what registers the behavior
    std::optional<ScriptCall> tick;
    std::optional<ScriptCall> focus;
    std::optional<ScriptCall> trigger;
    std::optional<ScriptCall> filter;
    std::optional<ScriptCall> once;
};

// What `pathable` does beyond being drawn, through its categories, or null when it does
// nothing more — which is most markers, so they carry a pointer and no more. A trail takes
// the filters and the script only, as StandardTrail_Behaviors.cs registers no modifier and
// no `behavior`.
std::shared_ptr<const Behaviors> bakeBehaviors(const pack::Pack& pack, const pack::Pathable& pathable, bool is_marker);

} // namespace viewer::behaviors
