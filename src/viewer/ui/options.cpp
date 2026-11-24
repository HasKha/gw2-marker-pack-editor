#include "viewer/ui/options.h"

#include <imgui.h>

#include <algorithm>
#include <iterator>

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
    // In Occlusion's order.
    const char* const occlusions[] = {"None", "Behind Characters", "Behind Characters and Terrain"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    changed |= ImGui::Combo("Marker occlusion", &settings->occlusion, occlusions, IM_ARRAYSIZE(occlusions));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Default occlusion, if not set by the pack.\n"
            "Needs the GoodRender addon.\n"
            "Some packs decide this for their own markers.");
    }
    // TODO: ^ describe the tag for marker creators.

    changed |= ImGui::Checkbox("Allow markers to hide themselves", &settings->allow_auto_hide);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When off, every marker shows, including those hidden by festivals, schedules or after you used them.");
    changed |= ImGui::Checkbox("Allow markers to animate", &settings->allow_animation);
    changed |= ImGui::Checkbox("Smart categories", &settings->smart_categories);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Leaves categories with nothing on the current map out of the menu.");
    changed |= ImGui::Checkbox("Show what interacting does", &settings->show_interact_prompt);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A prompt near a marker you can interact with, saying what your Interact key will do.");
    changed |= ImGui::Checkbox("Show tooltips for achievements", &settings->achievement_tooltips);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows the achievement when you point at a category in the menu that is tied to one.");

    static constexpr const char* kInfoDisplays[] = {"Default", "Without background", "Never"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    changed |= ImGui::Combo("Marker info", &settings->info_display, kInfoDisplays, IM_ARRAYSIZE(kInfoDisplays));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Text some markers show when you are near them.");

    static constexpr const char* kConsents[] = {"Always", "Only when I interact", "Never"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    changed |= ImGui::Combo("Markers may copy to the clipboard", &settings->clipboard_consent, kConsents, IM_ARRAYSIZE(kConsents));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Some markers copy text, such as a waypoint code, for you to paste in chat.");

    ImGui::Checkbox("Show hidden markers faded, to unhide them", show_hidden);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Interact with a faded marker to bring it back. Turns itself off when the game restarts.");
    return changed;
}

namespace {

// The maps' DPI scale, settings::DpiScaling, with Custom's factor under it when chosen.
bool dpiScalingCombo(settings::Settings* settings) {
    static constexpr const char* kNames[] = {"None", "Nexus", "System", "Custom"};
    static constexpr const char* kTips[] = {
        "No DPI scaling: for the game's DPI Scaling option off.",
        "Nexus's scale, which follows Nexus's own DPI Scaling option:\nright when that is set as the game's is.",
        "Windows' display scale: for the game's DPI Scaling option on.",
        "A scale of your own.",
    };
    bool changed = false;
    int* mode = &settings->map_dpi_scaling;
    if (ImGui::BeginCombo("DPI scaling", kNames[*mode])) {
        for (int i = 0; i < static_cast<int>(std::size(kNames)); ++i) {
            if (ImGui::Selectable(kNames[i], *mode == i) && *mode != i) {
                *mode = i;
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kTips[i]);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Must match how the game scales its maps, or markers are cut off at the minimap's edge\nand drift as the map pans.");
    }
    if (*mode == static_cast<int>(settings::DpiScaling::Custom)) {
        if (ImGui::InputFloat("Custom scale", &settings->map_dpi_custom, 0.05f, 0.25f, "%.2f")) {
            settings->map_dpi_custom = std::clamp(settings->map_dpi_custom, 0.5f, 4.0f);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("1.25 for Windows' 125%%.");
    }
    return changed;
}

// The settings one map has of its own, under its name.
bool drawOneMap(const char* name, int* markers, int* trails, float* opacity) {
    ImGui::PushID(name);
    ImGui::TextDisabled("%s", name);
    ImGui::Indent();
    bool changed = visibilityCombo("Markers", markers);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Default lets each pack decide; Always and Never override it.");
    changed |= visibilityCombo("Trails", trails);
    changed |= slider("Opacity", opacity, 0.0f, 1.0f, "%.2f");
    ImGui::Unindent();
    ImGui::PopID();
    return changed;
}

// The above/below checkbox, its label showing the two images the maps draw, which a click
// toggles as a checkbox's own label does. Without an image its word stands alone.
bool aboveBelowCheckbox(bool* value, ImTextureID above, ImTextureID below) {
    bool changed = ImGui::Checkbox("##above_below", value);
    bool hovered = ImGui::IsItemHovered();
    bool clicked = false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float size = ImGui::GetFontSize();
    const auto item = [&] {
        hovered |= ImGui::IsItemHovered();
        clicked |= ImGui::IsItemClicked();
    };
    const auto text = [&](const char* part, float spacing) {
        ImGui::SameLine(0.0f, spacing);
        ImGui::TextUnformatted(part);
        item();
    };
    const auto image = [&](ImTextureID texture) {
        if (!texture) return;
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x * 0.5f);
        // Down to where the text of a frame-high line starts, as the words beside it are.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.FramePadding.y);
        ImGui::Image(texture, ImVec2(size, size));
        item();
    };
    text("Show above", style.ItemInnerSpacing.x);
    image(above);
    text("/below", above ? style.ItemInnerSpacing.x * 0.5f : 0.0f);
    image(below);
    text("indicators", style.ItemInnerSpacing.x);
    if (clicked) {
        *value = !*value;
        changed = true;
    }
    if (hovered) ImGui::SetTooltip("Marks a map icon well above or below you.");
    return changed;
}

} // namespace

bool drawMaps(settings::Settings* settings, ImTextureID above, ImTextureID below) {
    // TODO: can we not share all those settings? and have minimap and map settings be independent.
    bool changed = ImGui::Checkbox("Show markers on maps", &settings->map_pathables);
    changed |= drawOneMap("Minimap", &settings->minimap_marker_visibility, &settings->minimap_trail_visibility, &settings->minimap_opacity);
    changed |= drawOneMap("Full map", &settings->map_marker_visibility, &settings->map_trail_visibility, &settings->map_opacity);
    changed |= slider("Trail width", &settings->map_trail_width, 0.5f, 4.5f, "%.1f");
    changed |= aboveBelowCheckbox(&settings->map_above_below, above, below);
    changed |= ImGui::Checkbox("Fade trail segments high above or below", &settings->map_fade_far_trails);
    changed |= ImGui::Checkbox("Show tooltips", &settings->map_tooltips);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold Shift to see a marker's categories instead; right-click to hide one.");
    changed |= dpiScalingCombo(settings);
    return changed;
}

KeyAction drawApiKey(const std::string& registered, char* field, size_t field_size, bool checking, const std::string& status) {
    KeyAction action = KeyAction::None;

    // How a key is made, with the applications page a link: imgui 1.80 has none of its own,
    // so it is text in the link colour, underlined and with a hand while hovered.
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::Text("Navigate to the official");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    ImGui::Text("applications page.");
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("account.arena.net/applications");
    }
    if (ImGui::IsItemClicked()) action = KeyAction::OpenApplications;

    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::Text("Click \"New Key\".");
    // The permissions api_key::PERMISSIONS names.

    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextWrapped("Select \"account\" and \"progression\" and press the \"CREATE API KEY\" button.");

    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::Text("Copy the new key.");

    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextWrapped("Paste the key in the field below and press \"Register\".");

    // The field, or the key registered, as wide as the window leaves beside the wider button,
    // so that the line keeps its shape when one replaces the other.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float button = std::max(ImGui::CalcTextSize("Register").x, ImGui::CalcTextSize("Remove").x) + style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - button - style.ItemSpacing.x, ImGui::GetFontSize() * 8.0f));
    if (registered.empty()) {
        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | (checking ? ImGuiInputTextFlags_ReadOnly : 0);
        const bool entered = ImGui::InputText("##api_key", field, field_size, flags);
        ImGui::SameLine();
        if ((ImGui::Button("Register") || entered) && !checking) action = KeyAction::Register;
    } else {
        // As many asterisks as the key has characters, and nothing to copy it out with.
        std::string shown = registered;
        ImGui::InputText(
            "##api_key_registered",
            shown.data(),
            shown.size() + 1,
            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_Password);
        ImGui::SameLine();
        if (ImGui::Button("Remove")) action = KeyAction::Remove;
    }

    if (checking) {
        ImGui::Text("Checking the key...");
    } else if (!status.empty()) {
        ImGui::TextWrapped("%s", status.c_str());
    }

    if (ImGui::Button("Clear cached API data")) action = KeyAction::ClearCache;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Forgets your saved account progress and achievement details,\n"
            "and downloads them again.");
    }
    return action;
}

} // namespace viewer::ui::options
