#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "pack/model.h"

// A pathable's attributes as the bake reads them: every one it reads, through the category chain,
// looked up by a slot fixed when the code is compiled rather than by name.
namespace viewer {

// Every attribute the bake and the behaviors read, as keys. A key read anywhere else in the bake
// has to be added here, or the read does not build.
inline constexpr std::string_view kBakedKeys[] = {
    // Markers and trails both
    "alpha",
    "color",
    "tint",
    "guid",
    "occlusion",
    "cull",
    "fadenear",
    "fadefar",
    "ingamevisibility",
    "mapvisibility",
    "minimapvisibility",
    // Markers
    "xpos",
    "ypos",
    "zpos",
    "heightoffset",
    "rotate",
    "rotate-x",
    "rotate-y",
    "rotate-z",
    "mapid",
    "iconsize",
    "minsize",
    "maxsize",
    "iconfile",
    "tip-name",
    "tip-description",
    "mapdisplaysize",
    "scaleonmapwithzoom",
    "triggerrange",
    "inforange",
    "resetlength",
    // Trails
    "trailscale",
    "animspeed",
    "iswall",
    "texture",
    "traildata",
    // Behaviors
    "festival",
    "mount",
    "profession",
    "race",
    "specialization",
    "maptype",
    "schedule",
    "schedule-duration",
    "raid",
    "achievementid",
    "achievementbit",
    "behavior",
    "invertbehavior",
    "autotrigger",
    "info",
    "bounce",
    "bounce-delay",
    "bounce-height",
    "bounce-duration",
    "copy",
    "copy-message",
    "togglecategory",
    "toggle",
    "show",
    "hide",
    "resetguid",
    "script-tick",
    "script-focus",
    "script-trigger",
    "script-filter",
    "script-once"};

inline constexpr size_t kBakedKeyCount = std::size(kBakedKeys);

// One of kBakedKeys, made from its name as the code is compiled: `values.value("xpos")` costs
// an array read, and a name the list lacks is a build error.
struct BakedKey {
    size_t slot = 0;

    consteval BakedKey(const char* key)
        : slot(slotOf(key)) {}

private:
    static consteval size_t slotOf(std::string_view key) {
        for (size_t i = 0; i < kBakedKeyCount; ++i) {
            if (kBakedKeys[i] == key) return i;
        }
        throw "not in kBakedKeys"; // not a constant expression, so a key the list lacks does not build
    }
};

// One pathable's effective value of every baked key: its own, else its nearest category's, as
// pack::Pack::effectiveValue gives it. Points into the pack, which must outlive it unchanged.
struct EffectiveValues {
    std::array<const std::string*, kBakedKeyCount> values{};

    const std::string* value(BakedKey key) const { return values[key.slot]; }

    // The value read as Pack::effectiveFloat, effectiveBool and effectiveInt read it: the
    // attribute table's default when it is missing or does not read.
    float number(BakedKey key) const;
    bool flag(BakedKey key) const;
    int integer(BakedKey key) const;
};

// One pathable alone, a lookup per key: for a marker a script creates, or the gizmo's one. A
// pack's every pathable is read through an EffectiveTable.
EffectiveValues effectiveValues(const pack::Pack& pack, const pack::Pathable& pathable);

// Every category's effective values, each resolved once from its parent's, so that a pathable
// costs its own few attributes rather than a walk up the chain per key. Points into the pack,
// which must outlive it unchanged.
class EffectiveTable
{
public:
    explicit EffectiveTable(const pack::Pack& pack);

    // As effectiveValues(pack, pathable), for a pathable whose category is one of the pack's.
    EffectiveValues of(const pack::Pathable& pathable) const;

private:
    std::vector<EffectiveValues> rows; // one per category, by index
};

} // namespace viewer
