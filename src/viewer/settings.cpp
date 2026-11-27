#include "viewer/settings.h"

#include <algorithm>

#include "util/json_file.h"

namespace viewer::settings {

namespace {

// Each setting by the name it is saved under, so that reading and writing go through one list.
template <class T>
struct Field {
    const char* key;
    T Settings::* member;
    T min{};
    T max{};
};

constexpr Field<bool> kBools[] = {
    {"import_offered", &Settings::import_offered},
    {"run_scripts", &Settings::run_scripts},
    {"allow_auto_hide", &Settings::allow_auto_hide},
    {"allow_animation", &Settings::allow_animation},
    {"smart_categories", &Settings::smart_categories},
    {"show_interact_prompt", &Settings::show_interact_prompt},
    {"achievement_tooltips", &Settings::achievement_tooltips},
    {"menu_select_all", &Settings::menu_select_all},
    {"menu_in_nexus", &Settings::menu_in_nexus},
    {"menu_icon", &Settings::menu_icon},
    {"world_pathables", &Settings::world_pathables},
    {"combat_fade", &Settings::combat_fade},
    {"map_pathables", &Settings::map_pathables},
    {"map_above_below", &Settings::map_above_below},
    {"map_fade_far_trails", &Settings::map_fade_far_trails},
    {"map_tooltips", &Settings::map_tooltips},
    {"log_slow_frames", &Settings::log_slow_frames},
    {"info_movable", &Settings::info_movable},
};

// With the range each is clamped to, so that a hand-edited file cannot name a choice that
// does not exist.
constexpr Field<int> kInts[] = {
    {"clipboard_consent", &Settings::clipboard_consent, 0, 2},
    {"occlusion", &Settings::occlusion, 0, 2},
    {"info_display", &Settings::info_display, 0, 2},
    {"map_marker_visibility", &Settings::map_marker_visibility, 0, 2},
    {"map_trail_visibility", &Settings::map_trail_visibility, 0, 2},
    {"minimap_marker_visibility", &Settings::minimap_marker_visibility, 0, 2},
    {"minimap_trail_visibility", &Settings::minimap_trail_visibility, 0, 2},
    {"map_dpi_scaling", &Settings::map_dpi_scaling, 0, 3},
};

// Pathing's SetRange for each.
constexpr Field<float> kFloats[] = {
    {"max_opacity", &Settings::max_opacity, 0.0f, 1.0f},
    {"view_distance", &Settings::view_distance, 25.0f, 50000.0f},
    {"view_fade", &Settings::view_fade, 0.0f, 50000.0f}, // ours: up to the farthest view distance
    {"max_trail_speed", &Settings::max_trail_speed, 0.0f, 10.0f},
    {"marker_scale", &Settings::marker_scale, 0.1f, 4.0f},
    {"map_opacity", &Settings::map_opacity, 0.0f, 1.0f},
    {"minimap_opacity", &Settings::minimap_opacity, 0.0f, 1.0f},
    {"map_trail_width", &Settings::map_trail_width, 0.5f, 4.5f},
    {"map_dpi_custom", &Settings::map_dpi_custom, 0.5f, 4.0f},
    // From a few times our usual work to a frame at 10 fps.
    {"slow_frame_ms", &Settings::slow_frame_ms, 1.0f, 100.0f},
    // Ours, as the options page's sliders take them. The text may go below the 20 px floor
    // (docs/code_style.md) by the player's choice, never by default; the place anywhere on a
    // screen up to 8K, as interaction keeps the box on the screen it is drawn on.
    {"info_text_pixels", &Settings::info_text_pixels, 10.0f, 64.0f},
    {"info_margin", &Settings::info_margin, 0.0f, 200.0f},
    {"info_x", &Settings::info_x, 0.0f, 8000.0f},
    {"info_y", &Settings::info_y, 0.0f, 8000.0f},
};

// The info box's background as saved: [r, g, b, a].
constexpr const char* kInfoBackground = "info_background";

} // namespace

Settings load(const std::filesystem::path& folder) {
    Settings settings;
    if (folder.empty()) return settings; // Nexus gave no folder: nothing to read

    if (const std::optional<nlohmann::json> parsed = util::json_file::readObject(folder / FILE_NAME)) {
        for (const Field<bool>& field : kBools) {
            settings.*field.member = util::json_file::readBool(*parsed, field.key, settings.*field.member);
        }
        // "Hide markers behind terrain", the checkbox "occlusion" took the place of: on was Scene.
        settings.occlusion = util::json_file::readBool(*parsed, "behind_terrain", false) ? 2 : 0;
        for (const Field<int>& field : kInts) {
            settings.*field.member = std::clamp(util::json_file::readInt(*parsed, field.key, settings.*field.member), field.min, field.max);
        }
        for (const Field<float>& field : kFloats) {
            settings.*field.member =
                std::clamp(util::json_file::readFloat(*parsed, field.key, settings.*field.member), field.min, field.max);
        }
        // Four numbers, each clamped to 0 to 1; anything else keeps the default.
        if (const auto saved = parsed->find(kInfoBackground);
            saved != parsed->end() && saved->is_array() && saved->size() == settings.info_background.size() &&
            std::all_of(saved->begin(), saved->end(), [](const nlohmann::json& channel) { return channel.is_number(); })) {
            for (size_t i = 0; i < settings.info_background.size(); ++i) {
                settings.info_background[i] = std::clamp((*saved)[i].get<float>(), 0.0f, 1.0f);
            }
        }
        settings.api_key = util::json_file::readString(*parsed, "api_key");
    }
    return settings;
}

bool save(const std::filesystem::path& folder, const Settings& settings) {
    if (folder.empty()) return false;

    nlohmann::json out;
    for (const Field<bool>& field : kBools) out[field.key] = settings.*field.member;
    for (const Field<int>& field : kInts) out[field.key] = settings.*field.member;
    for (const Field<float>& field : kFloats) out[field.key] = settings.*field.member;
    out[kInfoBackground] = settings.info_background;
    out["api_key"] = settings.api_key;
    return util::json_file::write(folder / FILE_NAME, out);
}

} // namespace viewer::settings
