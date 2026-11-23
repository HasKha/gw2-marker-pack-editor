#include "viewer/ui/options.h"

#include <imgui.h>

namespace viewer::ui::options {

namespace {

// Pathing's MapVisibilityLevel, as map_scene::Visibility numbers it.
constexpr const char* kVisibilities[] = {"Default", "Always", "Never"};

bool visibilityCombo(const char* label, int* value) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    return ImGui::Combo(label, value, kVisibilities, IM_ARRAYSIZE(kVisibilities));
}

bool slider(const char* label, float* value, float min, float max, const char* format) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    return ImGui::SliderFloat(label, value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
}

} // namespace

bool drawPack(settings::Settings* settings, bool* show_hidden) {
    bool changed = ImGui::Checkbox("Show markers in the world", &settings->world_pathables);
    changed |= slider("Max opacity", &settings->max_opacity, 0.0f, 1.0f, "%.2f");
    changed |= slider("Max view distance", &settings->view_distance, 25.0f, 50000.0f, "%.0f");
    changed |= slider("Max trail animation speed", &settings->max_trail_speed, 0.0f, 10.0f, "%.1f");
    changed |= slider("Marker scale", &settings->marker_scale, 0.1f, 4.0f, "%.2f");
    changed |= ImGui::Checkbox("Fade markers and trails during combat", &settings->combat_fade);
    changed |= ImGui::Checkbox("Hide markers behind terrain", &settings->behind_terrain);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "With GoodRender: walls and hills hide markers and trails behind them.\n"
            "Off, they draw over the world and under the game's windows.\n"
            "A pack can choose for its own markers.");
    }

    changed |= ImGui::Checkbox("Allow markers to hide themselves", &settings->allow_auto_hide);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off shows every marker, whatever festival, character or interaction would hide it.");
    changed |= ImGui::Checkbox("Allow markers to animate", &settings->allow_animation);
    changed |= ImGui::Checkbox("Smart categories", &settings->smart_categories);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The menu leaves out categories with nothing on the current map.");
    changed |= ImGui::Checkbox("Show what interacting does", &settings->show_interact_prompt);

    static constexpr const char* kInfoDisplays[] = {"Default", "Without background", "Never"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    changed |= ImGui::Combo("Marker info", &settings->info_display, kInfoDisplays, IM_ARRAYSIZE(kInfoDisplays));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What some markers say when you are near them.");

    static constexpr const char* kConsents[] = {"Always", "Only when I interact", "Never"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    changed |= ImGui::Combo("Markers may copy to the clipboard", &settings->clipboard_consent, kConsents, IM_ARRAYSIZE(kConsents));

    ImGui::Checkbox("Show hidden markers faded, to unhide them", show_hidden);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("For this session only, as in Pathing.");
    return changed;
}

namespace {

void sharedTooltip(const char* also = nullptr) {
    if (!ImGui::IsItemHovered()) return;
    if (also) {
        ImGui::SetTooltip("%s\nShared by the minimap and the full map.", also);
    } else {
        ImGui::SetTooltip("Shared by the minimap and the full map.");
    }
}

// One map's section: its own settings, then those both maps share, which the other section shows too.
bool drawMap(const char* id, settings::Settings* settings, int* markers, int* trails, float* opacity) {
    ImGui::PushID(id);
    bool changed = ImGui::Checkbox("Show markers on maps", &settings->map_pathables);
    sharedTooltip();
    if (settings->map_pathables) {
        changed |= visibilityCombo("Markers", markers);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Default follows each marker's own choice; Always and Never override it.");
        changed |= visibilityCombo("Trails", trails);
        changed |= slider("Opacity", opacity, 0.0f, 1.0f, "%.2f");
        changed |= slider("Trail width", &settings->map_trail_width, 0.5f, 4.5f, "%.1f");
        sharedTooltip();
        changed |= ImGui::Checkbox("Show above/below indicators", &settings->map_above_below);
        sharedTooltip();
        changed |= ImGui::Checkbox("Fade trail segments high above or below", &settings->map_fade_far_trails);
        sharedTooltip();
        changed |= ImGui::Checkbox("Show tooltips", &settings->map_tooltips);
        sharedTooltip("Hold Shift to see a marker's categories instead; right-click to hide one.");
    }
    ImGui::PopID();
    return changed;
}

} // namespace

bool drawMinimap(settings::Settings* settings) {
    return drawMap(
        "minimap",
        settings,
        &settings->minimap_marker_visibility,
        &settings->minimap_trail_visibility,
        &settings->minimap_opacity);
}

bool drawFullMap(settings::Settings* settings) {
    return drawMap("full_map", settings, &settings->map_marker_visibility, &settings->map_trail_visibility, &settings->map_opacity);
}

} // namespace viewer::ui::options
