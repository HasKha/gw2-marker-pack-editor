#include "pack/attributes.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <unordered_map>

namespace pack {

namespace {

// Shorthand so the table below reads as data rather than as constructor calls.
constexpr ValueKind kStr = ValueKind::String;
constexpr ValueKind kInt = ValueKind::Int;
constexpr ValueKind kFloat = ValueKind::Float;
constexpr ValueKind kBool = ValueKind::Bool;
constexpr ValueKind kColor = ValueKind::Color;
constexpr ValueKind kPath = ValueKind::Path;
constexpr ValueKind kGuid = ValueKind::Guid;
constexpr ValueKind kVec3 = ValueKind::Vec3;
constexpr ValueKind kEnum = ValueKind::Enum;

// Defaults are from Pathing/State/UserResources/Population/*.cs, which cite GW2TacO's
// gw2tactical.h. Where markers and trails disagree (fadenear/fadefar) the marker value
// is listed and the difference is called out in the doc string — see docs/format.md.
const std::vector<AttributeDef>& table() {
    static const std::vector<AttributeDef> t = {
        // ── Structural ────────────────────────────────────────────────────────
        {"name", "name", kStr, kAppliesCategory, false, "", "Category id segment. Joined with dots to form a marker's type."},
        {"displayname", "DisplayName", kStr, kAppliesCategory, false, "", "Human-readable category name."},
        {"type", "type", kStr, kAppliesPathable, false, "", "Dot-joined category id this pathable belongs to."},
        {"mapid", "MapID", kInt, kAppliesPathable, true, "", "Map the pathable appears on."},
        {"guid", "GUID", kGuid, kAppliesPathable, false, "", "Base64 of 16 raw bytes. Identity across pack versions."},
        {"xpos", "xpos", kFloat, kAppliesPoi, false, "", "Position X, in metres."},
        {"ypos", "ypos", kFloat, kAppliesPoi, false, "", "Position Y (up), in metres."},
        {"zpos", "zpos", kFloat, kAppliesPoi, false, "", "Position Z, in metres."},
        {"traildata", "trailData", kPath, kAppliesTrail, true, "", "Path to the .trl file holding this trail's points."},

        // ── Category behaviour ────────────────────────────────────────────────
        {"isseparator", "IsSeparator", kBool, kAppliesCategory, false, "0", "Render as a non-selectable heading in the menu."},
        {"ishidden", "IsHidden", kBool, kAppliesCategory, false, "0", "Hide the category from the menu entirely."},
        {"defaulttoggle", "DefaultToggle", kBool, kAppliesCategory, false, "1", "Whether the category starts enabled."},

        // ── Appearance ────────────────────────────────────────────────────────
        {"iconfile", "iconFile", kPath, kAppliesPoi, true, "", "Marker icon, relative to the pack root."},
        {"texture", "texture", kPath, kAppliesTrail, true, "", "Trail ribbon texture, relative to the pack root."},
        {"iconsize", "iconSize", kFloat, kAppliesPoi, true, "1", "Icon scale. The quad is the unit square times iconSize*2."},
        {"alpha", "alpha", kFloat, kAppliesPathable, true, "1", "Opacity, 0..1."},
        {"color", "color", kColor, kAppliesPathable, true, "FFFFFFFF", "Tint. Alias of tint."},
        {"tint", "tint", kColor, kAppliesPathable, true, "FFFFFFFF", "Tint. Alias of color."},
        {"heightoffset", "heightOffset", kFloat, kAppliesPoi, true, "1.5", "Metres to lift the marker above its position."},
        {"minsize", "minSize", kFloat, kAppliesPoi, true, "5", "Lower clamp on on-screen size. Pixels, times 4."},
        {"maxsize", "maxSize", kFloat, kAppliesPoi, true, "2048", "Upper clamp on on-screen size. Pixels, times 4."},
        {"rotate", "rotate", kVec3, kAppliesPoi, true, "", "Fixed orientation \"x,y,z\" in degrees. Disables billboarding."},
        {"rotate-x", "rotate-x", kFloat, kAppliesPoi, true, "0", "Fixed X rotation, degrees."},
        {"rotate-y", "rotate-y", kFloat, kAppliesPoi, true, "0", "Fixed Y rotation, degrees."},
        {"rotate-z", "rotate-z", kFloat, kAppliesPoi, true, "0", "Fixed Z rotation, degrees."},
        {"cull", "cull", kEnum, kAppliesPathable, true, "none", "Face culling: none, clockwise, counterclockwise."},
        {"occlude", "occlude", kBool, kAppliesPoi, true, "0", "Draw opaque so world geometry can hide it."},
        {"iswall", "isWall", kBool, kAppliesTrail, true, "0", "Orient the trail ribbon vertically."},
        {"animspeed", "animSpeed", kFloat, kAppliesTrail, true, "1", "Texture scroll speed along the trail."},
        {"trailscale", "trailScale", kFloat, kAppliesTrail, true, "1", "Ribbon width and texture repeat scale."},

        // ── Fade and visibility ───────────────────────────────────────────────
        // Inches, not metres. Markers default to disabled (-1); trails to 700/900.
        {"fadenear", "fadeNear", kFloat, kAppliesPathable, true, "-1", "Distance (inches) where fading starts. Trails: 700."},
        {"fadefar", "fadeFar", kFloat, kAppliesPathable, true, "-1", "Distance (inches) where fully faded. Trails: 900."},
        {"canfade", "canFade", kBool, kAppliesPathable, true, "1", "Allow fading between the character and the camera."},
        {"ingamevisibility", "inGameVisibility", kBool, kAppliesPathable, true, "1", "Draw in the 3D world."},
        {"mapvisibility", "mapVisibility", kBool, kAppliesPathable, true, "1", "Draw on the full map."},
        {"minimapvisibility", "miniMapVisibility", kBool, kAppliesPathable, true, "1", "Draw on the compass/minimap."},
        {"mapdisplaysize", "mapDisplaySize", kFloat, kAppliesPoi, true, "20", "Icon size on the map."},
        {"scaleonmapwithzoom", "scaleOnMapWithZoom", kBool, kAppliesPoi, true, "1", "Scale the map icon with zoom level."},

        // ── Interaction and behaviour ─────────────────────────────────────────
        {"behavior", "behavior", kInt, kAppliesPathable, true, "0", "TacO behavior id controlling when the marker hides."},
        {"invertbehavior", "invertBehavior", kBool, kAppliesPathable, true, "0", "Invert the behavior's show/hide sense."},
        {"triggerrange", "triggerRange", kFloat, kAppliesPoi, true, "2", "Range (inches) at which the marker triggers."},
        {"autotrigger", "autoTrigger", kBool, kAppliesPoi, true, "0", "Trigger on approach rather than on interact."},
        {"resetlength", "resetLength", kFloat, kAppliesPathable, true, "0", "Seconds before a triggered marker returns."},
        {"resetguid", "resetGuid", kGuid, kAppliesPathable, true, "", "Marker whose trigger also resets this one."},
        {"togglecategory", "toggleCategory", kStr, kAppliesPathable, true, "", "Category toggled when this marker triggers."},
        {"toggle", "toggle", kStr, kAppliesPathable, true, "", "Category toggled on interact."},
        {"show", "show", kStr, kAppliesPathable, true, "", "Categories to show on trigger."},
        {"hide", "hide", kStr, kAppliesPathable, true, "", "Categories to hide on trigger."},
        {"copy", "copy", kStr, kAppliesPathable, true, "", "Text copied to the clipboard on interact."},
        {"copy-message", "copy-message", kStr, kAppliesPathable, true, "", "Confirmation shown after a copy."},
        {"info", "info", kStr, kAppliesPathable, true, "", "Text shown when in range."},
        {"inforange", "infoRange", kFloat, kAppliesPathable, true, "", "Range (inches) at which info shows."},

        // ── Labels ────────────────────────────────────────────────────────────
        {"title", "title", kStr, kAppliesPathable, true, "", "Label drawn with the marker."},
        {"title-color", "title-color", kColor, kAppliesPathable, true, "FFFFFFFF", "Label colour."},
        {"text", "text", kStr, kAppliesPathable, true, "", "Additional text."},
        {"tip-name", "tip-name", kStr, kAppliesPathable, true, "", "Tooltip heading."},
        {"tip-description", "tip-description", kStr, kAppliesPathable, true, "", "Tooltip body."},

        // ── Filters ───────────────────────────────────────────────────────────
        {"achievementid", "achievementId", kInt, kAppliesAll, true, "", "Hide once this achievement is done."},
        {"achievementbit", "achievementBit", kInt, kAppliesAll, true, "", "Hide once this achievement bit is done."},
        {"festival", "festival", kEnum, kAppliesAll, true, "", "Only show during these festivals."},
        {"maptype", "mapType", kEnum, kAppliesAll, true, "", "Only show on these map types."},
        {"mount", "mount", kEnum, kAppliesAll, true, "", "Only show on these mounts."},
        {"profession", "profession", kEnum, kAppliesAll, true, "", "Only show for these professions."},
        {"race", "race", kEnum, kAppliesAll, true, "", "Only show for these races."},
        {"specialization", "specialization", kEnum, kAppliesAll, true, "", "Only show for these specializations."},
        {"raid", "raid", kStr, kAppliesAll, true, "", "Tie visibility to raid encounter completion."},
        {"schedule", "schedule", kStr, kAppliesAll, true, "", "Cron expression controlling when the marker shows."},
        {"schedule-duration", "schedule-duration", kFloat, kAppliesAll, true, "", "Seconds the schedule window stays open."},

        // ── Scripting (Blish HUD) ─────────────────────────────────────────────
        {"bounce", "bounce", kEnum, kAppliesPoi, true, "", "Bounce animation mode."},
        {"bounce-height", "bounce-height", kFloat, kAppliesPoi, true, "", "Bounce height."},
        {"bounce-delay", "bounce-delay", kFloat, kAppliesPoi, true, "", "Delay before bouncing."},
        {"bounce-duration", "bounce-duration", kFloat, kAppliesPoi, true, "", "Bounce duration."},
        {"script-filter", "script-filter", kStr, kAppliesAll, true, "", "Lua predicate controlling visibility."},
        {"script-focus", "script-focus", kStr, kAppliesAll, true, "", "Lua run while the marker is focused."},
        {"script-once", "script-once", kStr, kAppliesAll, true, "", "Lua run once on load."},
        {"script-tick", "script-tick", kStr, kAppliesAll, true, "", "Lua run every tick."},
        {"script-trigger", "script-trigger", kStr, kAppliesAll, true, "", "Lua run on trigger."},
        {"edittag", "editTag", kStr, kAppliesAll, true, "", "Free-form tag used by editors to group markers."},
    };
    return t;
}

const std::unordered_map<std::string_view, const AttributeDef*>& index() {
    static const std::unordered_map<std::string_view, const AttributeDef*> map = [] {
        std::unordered_map<std::string_view, const AttributeDef*> m;
        for (const AttributeDef& def : table()) m.emplace(def.key, &def);
        return m;
    }();
    return map;
}

std::string_view trim(std::string_view text) {
    auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && is_space(text.front())) text.remove_prefix(1);
    while (!text.empty() && is_space(text.back())) text.remove_suffix(1);
    return text;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

const std::vector<AttributeDef>& attributeTable() {
    return table();
}

const AttributeDef* findAttribute(std::string_view name) {
    const std::string key = attributeKey(name);
    auto it = index().find(std::string_view(key));
    return it == index().end() ? nullptr : it->second;
}

std::string attributeKey(std::string_view name) {
    std::string key(trim(name));
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return key;
}

bool parseInt(std::string_view text, int* out) {
    text = trim(text);
    if (text.empty()) return false;
    int value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{}) return false;
    *out = value;
    return true;
}

bool parseFloat(std::string_view text, float* out) {
    text = trim(text);
    if (text.empty()) return false;
    // from_chars on float is locale-independent, which matters: packs are authored all
    // over the world but always use '.' as the decimal separator.
    float value = 0.0f;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{}) return false;
    *out = value;
    return true;
}

bool parseBool(std::string_view text, bool* out) {
    const std::string key = attributeKey(text);
    if (key == "1" || key == "true" || key == "yes") {
        *out = true;
        return true;
    }
    if (key == "0" || key == "false" || key == "no") {
        *out = false;
        return true;
    }
    // TacO wrote plain integers here too; anything non-zero counts as true.
    int value = 0;
    if (parseInt(key, &value)) {
        *out = value != 0;
        return true;
    }
    return false;
}

bool parseColor(std::string_view text, uint32_t* out_argb) {
    text = trim(text);
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8) return false;

    uint32_t value = 0;
    for (char c : text) {
        const int digit = hexDigit(c);
        if (digit < 0) return false;
        value = (value << 4) | static_cast<uint32_t>(digit);
    }

    // 6 digits is RRGGBB, so supply an opaque alpha.
    *out_argb = text.size() == 6 ? (0xFF000000u | value) : value;
    return true;
}

bool parseVec3(std::string_view text, glm::vec3* out) {
    glm::vec3 value(0.0f);
    size_t component = 0;
    size_t begin = 0;

    while (component < 3) {
        const size_t comma = text.find(',', begin);
        const std::string_view part = text.substr(begin, comma == std::string_view::npos ? std::string_view::npos : comma - begin);
        if (!parseFloat(part, &value[static_cast<glm::length_t>(component)])) return false;
        ++component;
        if (comma == std::string_view::npos) break;
        begin = comma + 1;
    }

    if (component != 3) return false;
    *out = value;
    return true;
}

bool parseGuid(std::string_view text, std::array<uint8_t, 16>* out) {
    text = trim(text);
    if (text.empty()) return false;

    std::array<uint8_t, 16> bytes{};
    size_t written = 0;
    uint32_t accumulator = 0;
    int bits = 0;

    for (char c : text) {
        if (c == '=') break;
        const size_t digit = kBase64Alphabet.find(c);
        if (digit == std::string_view::npos) return false;

        accumulator = (accumulator << 6) | static_cast<uint32_t>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (written >= bytes.size()) return false;
            bytes[written++] = static_cast<uint8_t>((accumulator >> bits) & 0xFF);
        }
    }

    if (written != bytes.size()) return false;
    *out = bytes;
    return true;
}

std::string guidToBase64(const std::array<uint8_t, 16>& guid) {
    std::string out;
    out.reserve(24);

    for (size_t i = 0; i < guid.size(); i += 3) {
        const uint32_t a = guid[i];
        const uint32_t b = i + 1 < guid.size() ? guid[i + 1] : 0;
        const uint32_t c = i + 2 < guid.size() ? guid[i + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;

        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back(i + 1 < guid.size() ? kBase64Alphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < guid.size() ? kBase64Alphabet[triple & 0x3F] : '=');
    }

    return out;
}

std::string formatFloat(float value) {
    // %g rather than %f: no trailing-zero noise, and enough precision to survive a
    // parse/format cycle without drifting.
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
    return buffer;
}

std::string formatVec3(const glm::vec3& value) {
    return formatFloat(value.x) + "," + formatFloat(value.y) + "," + formatFloat(value.z);
}

int defaultInt(std::string_view key, int fallback) {
    const AttributeDef* def = findAttribute(key);
    int value = fallback;
    if (def && parseInt(def->default_value, &value)) return value;
    return fallback;
}

float defaultFloat(std::string_view key, float fallback) {
    const AttributeDef* def = findAttribute(key);
    float value = fallback;
    if (def && parseFloat(def->default_value, &value)) return value;
    return fallback;
}

bool defaultBool(std::string_view key, bool fallback) {
    const AttributeDef* def = findAttribute(key);
    bool value = fallback;
    if (def && parseBool(def->default_value, &value)) return value;
    return fallback;
}

} // namespace pack
