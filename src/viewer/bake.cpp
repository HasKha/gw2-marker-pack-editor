#include "viewer/bake.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace viewer {

namespace {

constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

ImU32 tintOf(const pack::Pack& pack, const pack::Pathable& pathable) {
    uint32_t argb = 0xFFFFFFFFu;
    // `tint` and `color` are aliases; whichever the author used wins.
    const std::string* text = pack.effectiveValue(pathable, "color");
    if (!text) text = pack.effectiveValue(pathable, "tint");
    if (text) pack::parseColor(*text, &argb);

    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

std::string stringOf(const pack::Pack& pack, const pack::Pathable& pathable, const char* key) {
    const std::string* value = pack.effectiveValue(pathable, key);
    return value ? *value : std::string();
}

// `guid` is the one identity attribute that inherits, as format.md says of every pathable
// attribute in Pathing. Empty or missing gets a fresh one (Populate_Guid).
Guid guidOf(const pack::Pack& pack, const pack::Pathable& pathable) {
    const std::string* text = pack.effectiveValue(pathable, "guid");
    return text && !text->empty() ? guidFromText(*text) : newGuid();
}

// rotate="x,y,z" with any number of components; Blish fills the rest with zero.
void applyRotateList(std::string_view text, glm::vec3* out) {
    size_t component = 0;
    size_t begin = 0;
    while (component < 3) {
        const size_t comma = text.find(',', begin);
        const std::string_view part = text.substr(begin, comma == std::string_view::npos ? std::string_view::npos : comma - begin);

        float degrees = 0.0f;
        if (pack::parseFloat(part, &degrees)) (*out)[static_cast<glm::length_t>(component)] = degrees * kDegreesToRadians;

        ++component;
        if (comma == std::string_view::npos) break;
        begin = comma + 1;
    }
}

// Mirrors Pathing's Populate_Rotation: degrees in, radians out, and the presence of *any*
// rotate attribute is what disables billboarding.
//
// Note it takes rotate="90" the way applyRotateList does — one component, the rest zero —
// rather than demanding all three. Blish is the lenient one here, so we are too.
bool resolveRotation(const pack::Pack& pack, const pack::Pathable& pathable, glm::vec3* out) {
    bool present = false;
    *out = glm::vec3(0.0f);

    if (const std::string* text = pack.effectiveValue(pathable, "rotate")) {
        applyRotateList(*text, out);
        present = true;
    }

    // The per-axis forms are applied after, and win, matching the reference order.
    static constexpr const char* kAxisKeys[3] = {"rotate-x", "rotate-y", "rotate-z"};
    for (int axis = 0; axis < 3; ++axis) {
        if (const std::string* text = pack.effectiveValue(pathable, kAxisKeys[axis])) {
            float degrees = 0.0f;
            if (pack::parseFloat(*text, &degrees)) (*out)[axis] = degrees * kDegreesToRadians;
            present = true;
        }
    }

    return present;
}

std::optional<Occlusion> occlusionOf(const pack::Pack& pack, const pack::Pathable& pathable) {
    const std::string* value = pack.effectiveValue(pathable, "occlusion");
    return value ? parseOcclusion(*value) : std::nullopt;
}

// `value` is `lowercase_name` in any case.
bool equalsLowercase(std::string_view value, std::string_view lowercase_name) {
    return value.size() == lowercase_name.size() && std::equal(value.begin(), value.end(), lowercase_name.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == b;
           });
}

// GetValueAsEnum<CullDirection>: a name in any case, untrimmed; anything else is None.
Cull cullOf(const pack::Pack& pack, const pack::Pathable& pathable) {
    const std::string* value = pack.effectiveValue(pathable, "cull");
    if (!value) return Cull::None;
    if (equalsLowercase(*value, "clockwise")) return Cull::Clockwise;
    if (equalsLowercase(*value, "counterclockwise")) return Cull::CounterClockwise;
    return Cull::None;
}

} // namespace

std::optional<Occlusion> parseOcclusion(std::string_view value) {
    if (equalsLowercase(value, "none")) return Occlusion::None;
    if (equalsLowercase(value, "characters")) return Occlusion::Characters;
    if (equalsLowercase(value, "scene")) return Occlusion::Scene;
    return std::nullopt;
}

// ImVec2 predates constexpr constructors in the imgui Nexus ships, hence const.
const ImVec2 kRotatedMarkerUv[4] = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};
const ImVec2 kBillboardUv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};

bool markerTransform(const pack::Pack& pack, const pack::Pathable& pathable, MarkerTransform* out) {
    if (pathable.type != pack::PathableType::Poi) return false;

    // A POI without a position is an invisible anchor; the loader drops those and so do we.
    // See docs/format.md.
    const std::string* x = pathable.attributes.value("xpos");
    const std::string* y = pathable.attributes.value("ypos");
    const std::string* z = pathable.attributes.value("zpos");
    if (!x || !y || !z) return false;

    glm::vec3 position(0.0f);
    if (!pack::parseFloat(*x, &position.x)) return false;
    if (!pack::parseFloat(*y, &position.y)) return false;
    if (!pack::parseFloat(*z, &position.z)) return false;

    out->height_offset = pack.effectiveFloat(pathable, "heightoffset");
    // Where it is drawn, not its bare coordinate — which is what the gizmo has to sit on.
    position.y += out->height_offset;
    out->position = position;
    out->has_rotation = resolveRotation(pack, pathable, &out->rotation);
    return true;
}

bool bakeMarker(const pack::Pack& pack, const pack::Pathable& pathable, BakedMarker* out) {
    MarkerTransform transform;
    if (!markerTransform(pack, pathable, &transform)) return false;

    out->category = pathable.category;
    out->map_id = pack.effectiveInt(pathable, "mapid");
    out->position = transform.position;
    out->height_offset = transform.height_offset;
    out->has_rotation = transform.has_rotation;
    out->rotation = transform.rotation;
    out->icon_size = pack.effectiveFloat(pathable, "iconsize");
    out->alpha = pack.effectiveFloat(pathable, "alpha");
    out->min_size = pack.effectiveFloat(pathable, "minsize");
    out->max_size = pack.effectiveFloat(pathable, "maxsize");
    out->fade_near = pack.effectiveFloat(pathable, "fadenear");
    out->fade_far = pack.effectiveFloat(pathable, "fadefar");
    out->tint = tintOf(pack, pathable);
    out->icon = stringOf(pack, pathable, "iconfile");
    out->guid = guidOf(pack, pathable);
    const std::string* tip_name = pack.effectiveValue(pathable, "tip-name");
    if (tip_name) {
        out->tip_name = *tip_name;
    } else if (pathable.category != pack::kNoIndex) {
        out->tip_name = pack.categories[pathable.category].displayName();
    }
    out->tip_description = stringOf(pack, pathable, "tip-description");
    out->in_game_visibility = pack.effectiveBool(pathable, "ingamevisibility");
    out->map_visibility = pack.effectiveBool(pathable, "mapvisibility");
    out->minimap_visibility = pack.effectiveBool(pathable, "minimapvisibility");
    out->occlusion = occlusionOf(pack, pathable);
    out->cull = cullOf(pack, pathable);
    out->map_display_size = pack.effectiveFloat(pathable, "mapdisplaysize");
    out->scale_on_map_with_zoom = pack.effectiveBool(pathable, "scaleonmapwithzoom");
    // inforange is an alias, taken only while triggerrange is still the default.
    out->trigger_range = pack.effectiveFloat(pathable, "triggerrange");
    if (out->trigger_range == pack::defaultFloat("triggerrange")) {
        if (const std::string* info_range = pack.effectiveValue(pathable, "inforange")) pack::parseFloat(*info_range, &out->trigger_range);
    }
    out->reset_length = pack.effectiveFloat(pathable, "resetlength");
    out->behaviors = behaviors::bakeBehaviors(pack, pathable, true);
    return true;
}

BakedPathables bake(const pack::Pack& pack) {
    BakedPathables out;

    for (size_t i = 0; i < pack.pathables.size(); ++i) {
        const pack::Pathable& pathable = pack.pathables[i];
        if (pathable.grave.deleted) continue;
        if (pathable.type == pack::PathableType::Route) continue; // deprecated, never drawn

        if (pathable.type == pack::PathableType::Poi) {
            BakedMarker marker;
            if (!bakeMarker(pack, pathable, &marker)) continue;
            marker.pathable = i;
            out.markers.push_back(std::move(marker));
        } else {
            BakedTrail trail;
            trail.pathable = i;
            trail.category = pathable.category;
            trail.alpha = pack.effectiveFloat(pathable, "alpha");
            trail.scale = pack.effectiveFloat(pathable, "trailscale");
            trail.anim_speed = pack.effectiveFloat(pathable, "animspeed");
            trail.is_wall = pack.effectiveBool(pathable, "iswall");
            trail.tint = tintOf(pack, pathable);
            if (pack.effectiveValue(pathable, "color") || pack.effectiveValue(pathable, "tint")) trail.sample_colour = trail.tint;
            trail.texture = stringOf(pack, pathable, "texture");
            trail.sample_pending = trail.sample_colour == IM_COL32_WHITE && !trail.texture.empty();
            trail.data = stringOf(pack, pathable, "traildata");
            trail.guid = guidOf(pack, pathable);
            trail.in_game_visibility = pack.effectiveBool(pathable, "ingamevisibility");
            trail.map_visibility = pack.effectiveBool(pathable, "mapvisibility");
            trail.minimap_visibility = pack.effectiveBool(pathable, "minimapvisibility");
            trail.occlusion = occlusionOf(pack, pathable);
            trail.cull = cullOf(pack, pathable);
            trail.behaviors = behaviors::bakeBehaviors(pack, pathable, false);

            // Trails default to 700/900 where markers default to disabled, so the table's
            // marker defaults cannot be used here. See docs/format.md.
            const std::string* near_text = pack.effectiveValue(pathable, "fadenear");
            const std::string* far_text = pack.effectiveValue(pathable, "fadefar");
            if (!near_text || !pack::parseFloat(*near_text, &trail.fade_near)) trail.fade_near = 700.0f;
            if (!far_text || !pack::parseFloat(*far_text, &trail.fade_far)) trail.fade_far = 900.0f;

            out.trails.push_back(std::move(trail));
        }
    }
    return out;
}

std::vector<BakedCategory> bakeCategories(const pack::Pack& pack) {
    std::vector<BakedCategory> out;
    out.reserve(pack.categories.size());

    // Each flag is read the way TmfLib reads it — isseparator only on exactly "1", the other
    // two on anything but "0" — rather than through parseBool, so that "true" means here what
    // it means in Pathing.
    const auto flag = [](const pack::Category& category, const char* key, bool on_when_equal, const char* text) -> std::optional<bool> {
        const std::string* value = category.attributes.value(key);
        if (!value) return std::nullopt;
        return (*value == text) == on_when_equal;
    };

    for (const pack::Category& category : pack.categories) {
        BakedCategory baked;
        baked.id = category.id;
        baked.name = category.name;
        baked.parent = category.parent;
        if (const std::string* display = category.attributes.value("displayname")) baked.display_name = *display;
        baked.is_separator = flag(category, "isseparator", true, "1");
        baked.default_toggle = flag(category, "defaulttoggle", false, "0");
        baked.is_hidden = flag(category, "ishidden", false, "0");

        int number = 0;
        if (const std::string* id = category.attributes.value("achievementid"); id && pack::parseInt(*id, &number)) {
            baked.achievement_id = number;
        }
        // The nearest declaration's, this one's first; one that will not parse is none.
        for (size_t at = out.size(); at != pack::kNoIndex; at = pack.categories[at].parent) {
            if (const std::string* bit = pack.categories[at].attributes.value("achievementbit")) {
                if (pack::parseInt(*bit, &number)) baked.achievement_bit = number;
                break;
            }
        }
        out.push_back(std::move(baked));
    }
    return out;
}

float fadeFactor(float distance_inches, float fade_near, float fade_far) {
    if (fade_near < 0.0f || fade_far <= fade_near) return 1.0f;
    if (distance_inches >= fade_far) return 0.0f;
    if (distance_inches <= fade_near) return 1.0f;
    return 1.0f - (distance_inches - fade_near) / (fade_far - fade_near);
}

ImU32 applyOpacity(ImU32 colour, float opacity) {
    const float alpha = ((colour >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f * std::clamp(opacity, 0.0f, 1.0f);
    return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha * 255.0f + 0.5f) << IM_COL32_A_SHIFT);
}

// Blish builds this in its own Z-up space as
//   Scale(size*2) * RotX * RotY * RotZ * Translate(position)
// over a unit quad lying in the XY plane, i.e. flat on the ground facing up. Its space relates
// to the file's Y-up space by swapping Y and Z — the same swap TmfLib applies when reading
// .trl — so the offset is built in Blish's space and swapped back at the end. (Swapping two
// axes also flips handedness, which is exactly the LH-to-RH difference between Mumble's space
// and Blish's.)
void rotatedMarkerQuad(const glm::vec3& position, const glm::vec3& rotation, float icon_size, glm::vec3 out[4]) {
    const float half = icon_size; // the quad is the unit square scaled by size*2
    const float cx = std::cos(rotation.x), sx = std::sin(rotation.x);
    const float cy = std::cos(rotation.y), sy = std::sin(rotation.y);
    const float cz = std::cos(rotation.z), sz = std::sin(rotation.z);

    for (int i = 0; i < 4; ++i) {
        glm::vec3 p(kQuadCorners[i].x * half, kQuadCorners[i].y * half, 0.0f);

        // X, then Y, then Z, matching the reference's matrix order.
        p = glm::vec3(p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx);
        p = glm::vec3(p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy);
        p = glm::vec3(p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z);

        // Blish Z-up -> file/Mumble Y-up.
        out[i] = position + glm::vec3(p.x, p.z, p.y);
    }
}

} // namespace viewer
