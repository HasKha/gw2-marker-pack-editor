// The options page: Pathing's settings by Pathing's names, the API key, and what helps a pack
// author or a bug report. A method of the Session, so that each control acts as it is used.
#include "viewer/session.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <iterator>
#include <string>

#include "pack/attributes.h"
#include "util/path_text.h"
#include "viewer/desktop.h"
#include "viewer/packs/registry.h"
#include "viewer/ui/style.h"

namespace viewer {

namespace {

// Every slider, dropdown and field on the page at one width, in font heights, so their labels
// line up: room for the longest choice in the dropdowns, "Behind characters and terrain".
constexpr float kFieldWidthEms = 14.0f;

// Pathing's MapVisibilityLevel, as map_scene::Visibility numbers it. Its Default leaves it to
// each pack's own setting.
constexpr const char* kVisibilities[] = {"Pack decides", "Always", "Never"};

bool visibilityCombo(const char* label, int* value) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
    const bool changed = ImGui::Combo(label, value, kVisibilities, IM_ARRAYSIZE(kVisibilities));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pack decides leaves it to each pack; Always and Never override every pack.");
    return changed;
}

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
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
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
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
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
    ui::style::beginSubsection(icon, name);
    bool changed = visibilityCombo("Markers", markers);
    changed |= visibilityCombo("Trails", trails);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
    changed |= ImGui::SliderFloat("Opacity", opacity, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ui::style::endSubsection();
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

// The page's sections, each a tree node, the first three open until the player closes them: what a
// new player sets up, the API key among it, before what fine-tunes the drawing. Each control does
// what it says as it is used; a setting changed is saved within the frame, at the end of its section.
void Session::drawOptionsPage() {
    if (ui::style::section("interface", ICON_FA_DISPLAY, "Interface", true)) {
        if (ImGui::Checkbox("Show the menu in Nexus's right-click menu", &persisted.menu_in_nexus)) {
            showInNexusMenu(persisted.menu_in_nexus);
            saveSettings();
        }
        if (ImGui::Checkbox("Show an icon in Nexus's bar", &persisted.menu_icon)) {
            showIcon(persisted.menu_icon);
            saveSettings();
        }
        ImGui::TreePop();
    }

    if (ui::style::section("marker_packs", ICON_FA_BOX_ARCHIVE, "Marker Packs", true)) {
        if (ImGui::Checkbox("Enable Lua scripts", &persisted.run_scripts)) {
            saveSettings();
            // Off throws every handler away, and what scripts did to the map with them; on starts
            // them again from pack.lua.
            scripts->reset();
            replaceLiveMap(live_map.map_id);
        }

        // Manage and Reload side by side at one width, how loading goes under them while it does,
        // then Import.
        const std::string manage = ui::fonts::label(ICON_FA_BOX_ARCHIVE, "Manage Packs");
        const std::string reload = ui::fonts::label(ICON_FA_ROTATE_RIGHT, "Reload Packs");
        const float width = ui::style::buttonWidth({manage.c_str(), reload.c_str()});
        if (ui::style::button(manage.c_str(), true, width)) windows.catalog = true;
        ImGui::SameLine();
        if (ui::style::button(reload.c_str(), true, width)) pack_folder.reload(&installed);
        if (pack_folder.loading()) ImGui::Text("%s", pack_folder.loadingStatus(installed).c_str());
        if (ImGui::Button(ui::fonts::label(ICON_FA_FILE_IMPORT, "Import from Blish HUD...").c_str())) {
            // Reads again what Blish HUD has now: the window opens once the worker has read it
            // (drawImport).
            if (importer.running()) {
                windows.import = true;
            } else {
                importer.findOnWorker(registry::blishMarkersFolder(), &jobs);
            }
        }
        ImGui::TreePop();
    }

    if (ui::style::section("api_key", ICON_FA_KEY, "GW2 API Key", true)) {
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
        if (ui::style::link(api_key::APPLICATIONS_URL, "Opens it in your browser.")) desktop::openInBrowser(api_key::APPLICATIONS_URL);
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
        // so that the line keeps its shape when one replaces the other. While a check runs,
        // Register does nothing.
        const bool checking = registration.checking();
        const float button = ui::style::buttonWidth({"Register", "Remove"});
        ImGui::SetNextItemWidth(
            std::max(ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x, ImGui::GetFontSize() * 8.0f));
        if (persisted.api_key.empty()) {
            const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | (checking ? ImGuiInputTextFlags_ReadOnly : 0);
            const bool entered = ImGui::InputText("##api_key", &registration.field, flags);
            ImGui::SameLine();
            if ((ImGui::Button("Register", ImVec2(button, 0.0f)) || entered) && !checking) registration.submit();
        } else {
            // As many asterisks as the key has characters, and nothing to copy it out with.
            std::string shown = persisted.api_key;
            ImGui::InputText(
                "##api_key_registered",
                shown.data(),
                shown.size() + 1,
                ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            if (ImGui::Button("Remove", ImVec2(button, 0.0f))) {
                persisted.api_key.clear();
                saveSettings();
                // The progress kept was the key's: it goes with it.
                account_progress.setKey(persisted.api_key, utcNow());
                account_progress.clearCache();
                registration.status.clear();
            }
        }

        if (checking) {
            ImGui::Text("Checking the key...");
        } else if (!registration.status.empty()) {
            ImGui::TextWrapped("%s", registration.status.c_str());
        }

        if (ImGui::Button(ui::fonts::label(ICON_FA_ROTATE_RIGHT, "Clear Cached API Data").c_str())) {
            account_progress.clearCache();
            achievement_details.clear();
            registration.status = "Saved API data cleared.";
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Forgets your saved account progress and achievement details,\n"
                "and downloads them again.");
        }
        ImGui::TreePop();
    }

    if (ui::style::section("markers", ICON_FA_LOCATION_DOT, "Markers in World")) {
        bool changed = ImGui::Checkbox("Show markers in the world", &persisted.world_pathables);

        ui::style::beginSubsection(ICON_FA_EYE, "Appearance");
        ui::style::beginDisabled(!persisted.world_pathables);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
        changed |= ImGui::SliderFloat("Max opacity", &persisted.max_opacity, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
        changed |= ImGui::SliderFloat("Max view distance", &persisted.view_distance, 25.0f, 5000.0f, "%.0f");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
        changed |= ImGui::SliderFloat("Fade distance", &persisted.view_fade, 0.0f, 1000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "How far before the max view distance markers and trails start to fade out.\n"
                "0 cuts them off at the max view distance.");
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
        changed |= ImGui::SliderFloat("Marker scale", &persisted.marker_scale, 0.1f, 4.0f, "%.2f");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
        changed |=
            ImGui::SliderFloat("Max trail animation speed", &persisted.max_trail_speed, 0.0f, 10.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        // In Occlusion's order.
        static constexpr const char* kOcclusions[] = {"None", "Behind characters", "Behind characters and terrain"};
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
        changed |= ImGui::Combo("Marker occlusion", &persisted.occlusion, kOcclusions, IM_ARRAYSIZE(kOcclusions));
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
            ImGui::TextColored(ui::style::muted(), "<MarkerCategory name=\"walls\" occlusion=\"scene\"/>");
            ImGui::TreePop();
        }
        changed |= ImGui::Checkbox("Fade markers and trails during combat", &persisted.combat_fade);
        ui::style::endDisabled();
        ui::style::endSubsection();

        ui::style::beginSubsection(ICON_FA_WAND_MAGIC_SPARKLES, "Behavior");
        changed |= ImGui::Checkbox("Allow markers to hide themselves", &persisted.allow_auto_hide);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When off, every marker shows, including those hidden by festivals, schedules or after you used them.");
        changed |= ImGui::Checkbox("Allow markers to animate", &persisted.allow_animation);
        static constexpr const char* kConsents[] = {"Always", "Only when I interact", "Never"};
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
        changed |= ImGui::Combo("Markers may copy to the clipboard", &persisted.clipboard_consent, kConsents, IM_ARRAYSIZE(kConsents));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Some markers copy text, such as a waypoint code, for you to paste in chat.");
        // Pathing's "Temporarily Show Ghost Markers": for this session only, never saved.
        ImGui::Checkbox("Show hidden markers faded, to unhide them", &show_hidden);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Interact with a faded marker to bring it back. Turns itself off when the game restarts.");
        ui::style::endSubsection();

        ui::style::beginSubsection(ICON_FA_MESSAGE, "On-Screen Text");
        changed |= ImGui::Checkbox("Show what interacting does", &persisted.show_interact_prompt);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A prompt near a marker you can interact with, saying what your Interact key will do.");
        // In settings::InfoDisplay's order.
        static constexpr const char* kInfoDisplays[] = {"Shown", "Shown without background", "Hidden"};
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
        changed |= ImGui::Combo("Marker info text", &persisted.info_display, kInfoDisplays, IM_ARRAYSIZE(kInfoDisplays));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Text some markers show at the top of the screen while you are near them.");
        ui::style::endSubsection();

        ui::style::beginSubsection(ICON_FA_BARS, "Menu");
        changed |= ImGui::Checkbox("Smart categories", &persisted.smart_categories);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Leaves categories with nothing on the current map out of the menu.");
        changed |= ImGui::Checkbox("Show tooltips for achievements", &persisted.achievement_tooltips);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shows the achievement when you point at a category in the menu that is tied to one.");
        ui::style::endSubsection();
        if (changed) saveSettings();
        ImGui::TreePop();
    }

    if (ui::style::section("maps", ICON_FA_MAP_LOCATION_DOT, "Markers in Map and Minimap")) {
        bool changed = ImGui::Checkbox("Show markers on maps", &persisted.map_pathables);
        ui::style::beginDisabled(!persisted.map_pathables);
        changed |= drawOneMap(
            ICON_FA_COMPASS,
            "Minimap",
            &persisted.minimap_marker_visibility,
            &persisted.minimap_trail_visibility,
            &persisted.minimap_opacity);
        changed |=
            drawOneMap(ICON_FA_MAP, "Full Map", &persisted.map_marker_visibility, &persisted.map_trail_visibility, &persisted.map_opacity);
        ui::style::beginSubsection(ICON_FA_LAYER_GROUP, "Both Maps");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
        changed |= ImGui::SliderFloat("Trail width", &persisted.map_trail_width, 0.5f, 4.5f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        changed |= aboveBelowCheckbox(&persisted.map_above_below, aboveBelowImage(true), aboveBelowImage(false));
        changed |= ImGui::Checkbox("Fade trail segments high above or below", &persisted.map_fade_far_trails);
        changed |= ImGui::Checkbox("Show tooltips", &persisted.map_tooltips);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "A marker's name and how far away it is.\n"
                "Right-click a marker on the map to turn its categories on or off.");
        }
        changed |= dpiScalingCombo(&persisted);
        ui::style::endSubsection();
        ui::style::endDisabled();
        if (changed) saveSettings();
        ImGui::TreePop();
    }

    // For pack authors and bug reports. The images' counts walk every pack's cache, so the
    // figures are gathered only while the section is open.
    if (ui::style::section("development", ICON_FA_CODE, "Development")) {
        if (ImGui::Button(ui::fonts::label(ICON_FA_TERMINAL, "Open Lua Script Console").c_str())) windows.console = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Messages and errors from pack scripts.");
        ImGui::SameLine();

        ui::diagnostics::Figures figures;
        figures.service = good_render ? good_render->service() : good_render_sink::Service::None;
        figures.drawn = stats.last_drawn;
        figures.camera = stats.scene_camera;
        figures.full_map_up = mumble_state.is_map_open;
        figures.full_map_from_game = maps.fullMapFromGame();
        if (const std::shared_ptr<const Scene> scene = published()) figures.scene_items = scene->items.size();
        figures.present_ms = stats.present_times.last();
        figures.present_peak_ms = stats.present_times.peak();
        figures.map_id = live_map.map_id;
        figures.map_described = maps.find(live_map.map_id) != nullptr;
        figures.in_gameplay = inGameplay();
        figures.markers = live_map.markers.size();
        figures.trails = live_map.trails.size();
        figures.packs_found = pack_folder.found();
        figures.loading = pack_folder.loading();
        for (const std::unique_ptr<runtime::InstalledPack>& pack : installed.packs) {
            figures.packs.push_back(
                ui::diagnostics::PackFigures{util::pathText(pack->loaded->path.filename()), pack->loaded->error, pack->textures.counts()});
        }
        figures.uploads_images = textureUploads().images();
        figures.uploads_pixels = textureUploads().pixels();
        figures.jobs = jobs.unfinished();
        figures.lua_bytes = scripts->heapBytes();

        // The slow-frame log's setting, under the frame time it is about.
        ui::diagnostics::draw(ui::diagnostics::describe(figures), [this] {
            bool changed = ImGui::Checkbox("Log frames where Markus takes more than", &persisted.log_slow_frames);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "To Nexus's log, one line a second at most: how long Markus's own work took and where,\n"
                    "how long the whole frame was, and what was going on.");
            }
            ImGui::SameLine();
            ui::style::beginDisabled(!persisted.log_slow_frames);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFieldWidthEms);
            changed |= ImGui::SliderFloat(
                "##slow_frame_ms",
                &persisted.slow_frame_ms,
                1.0f,
                100.0f,
                "%.1f ms",
                ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            ui::style::endDisabled();
            if (changed) saveSettings();
        });
        ImGui::TreePop();
    }
}

} // namespace viewer
