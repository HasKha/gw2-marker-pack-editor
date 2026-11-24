#include "viewer/ui/options.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>

#include <algorithm>
#include <iterator>
#include <string>

#include "pack/attributes.h"
#include "viewer/api_key.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/style.h"

namespace viewer::ui::options {

namespace {

// Pathing's MapVisibilityLevel, as map_scene::Visibility numbers it. Its Default leaves it to
// each pack's own setting.
constexpr const char* kVisibilities[] = {"Pack decides", "Always", "Never"};

bool visibilityCombo(const char* label, int* value) {
    style::setFieldWidth();
    const bool changed = ImGui::Combo(label, value, kVisibilities, IM_ARRAYSIZE(kVisibilities));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pack decides leaves it to each pack; Always and Never override every pack.");
    return changed;
}

bool slider(const char* label, float* value, float min, float max, const char* format) {
    style::setFieldWidth();
    return ImGui::SliderFloat(label, value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
}

bool combo(const char* label, int* value, const char* const* items, int count) {
    style::setFieldWidth();
    return ImGui::Combo(label, value, items, count);
}

} // namespace

bool drawPack(settings::Settings* settings, bool* show_hidden) {
    bool changed = ImGui::Checkbox("Show markers in the world", &settings->world_pathables);

    style::subheading(ICON_FA_EYE, "Appearance");
    style::beginDisabled(!settings->world_pathables);
    changed |= slider("Max opacity", &settings->max_opacity, 0.0f, 1.0f, "%.2f");
    changed |= slider("Max view distance", &settings->view_distance, 25.0f, 50000.0f, "%.0f");
    changed |= slider("Marker scale", &settings->marker_scale, 0.1f, 4.0f, "%.2f");
    changed |= slider("Max trail animation speed", &settings->max_trail_speed, 0.0f, 10.0f, "%.1f");
    // In Occlusion's order.
    static constexpr const char* kOcclusions[] = {"None", "Behind characters", "Behind characters and terrain"};
    changed |= combo("Marker occlusion", &settings->occlusion, kOcclusions, IM_ARRAYSIZE(kOcclusions));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Default occlusion, if not set by the pack.\n"
            "Needs the GoodRender addon.\n"
            "Some packs decide this for their own markers.");
    }
    // How a pack decides it, for its author: the occlusion attribute, its words from the table,
    // which lists them in Occlusion's order too.
    if (ImGui::TreeNode("For Pack Authors")) {
        ImGui::TextWrapped(
            "A pack chooses this for its own markers and trails with the occlusion attribute, which overrides this option. "
            "Set on a category, everything in it inherits it.");
        const pack::AttributeDef* occlusion = pack::findAttribute("occlusion");
        for (size_t i = 0; occlusion && i < occlusion->choices.size() && i < std::size(kOcclusions); ++i) {
            const std::string choice(occlusion->choices[i]);
            ImGui::BulletText("occlusion=\"%s\": %s", choice.c_str(), kOcclusions[i]);
        }
        ImGui::TextWrapped(
            "Any other value, or no attribute, leaves it to this option. Without GoodRender, and in other marker addons, it does "
            "nothing. Not to be confused with occlude, which hides other markers.");
        ImGui::TextColored(style::muted(), "<MarkerCategory name=\"walls\" occlusion=\"scene\"/>");
        ImGui::TreePop();
    }
    changed |= ImGui::Checkbox("Fade markers and trails during combat", &settings->combat_fade);
    style::endDisabled();

    style::subheading(ICON_FA_WAND_MAGIC_SPARKLES, "Behavior");
    changed |= ImGui::Checkbox("Allow markers to hide themselves", &settings->allow_auto_hide);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When off, every marker shows, including those hidden by festivals, schedules or after you used them.");
    changed |= ImGui::Checkbox("Allow markers to animate", &settings->allow_animation);
    static constexpr const char* kConsents[] = {"Always", "Only when I interact", "Never"};
    changed |= combo("Markers may copy to the clipboard", &settings->clipboard_consent, kConsents, IM_ARRAYSIZE(kConsents));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Some markers copy text, such as a waypoint code, for you to paste in chat.");
    ImGui::Checkbox("Show hidden markers faded, to unhide them", show_hidden);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Interact with a faded marker to bring it back. Turns itself off when the game restarts.");

    style::subheading(ICON_FA_MESSAGE, "On-Screen Text");
    changed |= ImGui::Checkbox("Show what interacting does", &settings->show_interact_prompt);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A prompt near a marker you can interact with, saying what your Interact key will do.");
    // In settings::InfoDisplay's order.
    static constexpr const char* kInfoDisplays[] = {"Shown", "Shown without background", "Hidden"};
    changed |= combo("Marker info text", &settings->info_display, kInfoDisplays, IM_ARRAYSIZE(kInfoDisplays));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Text some markers show at the top of the screen while you are near them.");

    style::subheading(ICON_FA_BARS, "Menu");
    changed |= ImGui::Checkbox("Smart categories", &settings->smart_categories);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Leaves categories with nothing on the current map out of the menu.");
    changed |= ImGui::Checkbox("Show tooltips for achievements", &settings->achievement_tooltips);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows the achievement when you point at a category in the menu that is tied to one.");
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
    style::setFieldWidth();
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
        ImGui::SetTooltip(
            "Must match how the game scales its maps, or markers are cut off at the minimap's edge\nand drift as the map pans.");
    }
    if (*mode == static_cast<int>(settings::DpiScaling::Custom)) {
        style::setFieldWidth();
        if (ImGui::InputFloat("Custom scale", &settings->map_dpi_custom, 0.05f, 0.25f, "%.2f")) {
            settings->map_dpi_custom = std::clamp(settings->map_dpi_custom, 0.5f, 4.0f);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("1.25 for Windows' 125%%.");
    }
    return changed;
}

// The settings one map has of its own, under its name.
bool drawOneMap(const char* icon, const char* name, int* markers, int* trails, float* opacity) {
    ImGui::PushID(name);
    style::subheading(icon, name);
    bool changed = visibilityCombo("Markers", markers);
    changed |= visibilityCombo("Trails", trails);
    changed |= slider("Opacity", opacity, 0.0f, 1.0f, "%.2f");
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
    // Hovering and clicking a locked item read false, so a locked checkbox's label does nothing.
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
    style::beginDisabled(!settings->map_pathables);
    changed |= drawOneMap(
        ICON_FA_COMPASS,
        "Minimap",
        &settings->minimap_marker_visibility,
        &settings->minimap_trail_visibility,
        &settings->minimap_opacity);
    changed |=
        drawOneMap(ICON_FA_MAP, "Full Map", &settings->map_marker_visibility, &settings->map_trail_visibility, &settings->map_opacity);
    style::subheading(ICON_FA_LAYER_GROUP, "Both Maps");
    changed |= slider("Trail width", &settings->map_trail_width, 0.5f, 4.5f, "%.1f");
    changed |= aboveBelowCheckbox(&settings->map_above_below, above, below);
    changed |= ImGui::Checkbox("Fade trail segments high above or below", &settings->map_fade_far_trails);
    changed |= ImGui::Checkbox("Show tooltips", &settings->map_tooltips);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Hold Shift to see a marker's categories instead.\n"
            "Right-click a marker on the map to hide its category.");
    }
    changed |= dpiScalingCombo(settings);
    style::endDisabled();
    return changed;
}

KeyAction drawApiKey(const std::string& registered, char* field, size_t field_size, bool checking, const std::string& status) {
    KeyAction action = KeyAction::None;

    // How a key is made, the applications page's address a link. When the line has no room
    // for the address, it goes under the words, where the bullet's text starts.
    ImGui::Bullet();
    ImGui::SameLine();
    const float text_x = ImGui::GetCursorPosX();
    ImGui::TextUnformatted("Navigate to the official applications page");
    const float address = ImGui::CalcTextSize("()").x + ImGui::CalcTextSize(api_key::APPLICATIONS_URL).x;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + address <= right) {
        ImGui::SameLine();
    } else {
        ImGui::SetCursorPosX(text_x);
    }
    ImGui::TextUnformatted("(");
    ImGui::SameLine(0.0f, 0.0f);
    if (style::link(api_key::APPLICATIONS_URL, "Opens it in your browser.")) action = KeyAction::OpenApplications;
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(")");

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
    const float button = style::buttonWidth({"Register", "Remove"});
    ImGui::SetNextItemWidth(
        std::max(ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x, ImGui::GetFontSize() * 8.0f));
    if (registered.empty()) {
        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | (checking ? ImGuiInputTextFlags_ReadOnly : 0);
        const bool entered = ImGui::InputText("##api_key", field, field_size, flags);
        ImGui::SameLine();
        if ((ImGui::Button("Register", ImVec2(button, 0.0f)) || entered) && !checking) action = KeyAction::Register;
    } else {
        // As many asterisks as the key has characters, and nothing to copy it out with.
        std::string shown = registered;
        ImGui::InputText(
            "##api_key_registered",
            shown.data(),
            shown.size() + 1,
            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_Password);
        ImGui::SameLine();
        if (ImGui::Button("Remove", ImVec2(button, 0.0f))) action = KeyAction::Remove;
    }

    if (checking) {
        ImGui::Text("Checking the key...");
    } else if (!status.empty()) {
        ImGui::TextWrapped("%s", status.c_str());
    }

    if (ImGui::Button(fonts::label(ICON_FA_ROTATE_RIGHT, "Clear Cached API Data").c_str())) action = KeyAction::ClearCache;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Forgets your saved account progress and achievement details,\n"
            "and downloads them again.");
    }
    return action;
}

} // namespace viewer::ui::options
