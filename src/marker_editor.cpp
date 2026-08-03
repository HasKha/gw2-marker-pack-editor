#include "marker_editor.h"

#include <ShlObj.h>
#include <imgui.h>

#include <filesystem>
#include <optional>
#include <string>

#include "editor_state.h"
#include "file_dialog.h"
#include "pack/xml_reader.h"
#include "pack/zip.h"
#include "ui/property_panel.h"
#include "ui/tree_window.h"

namespace marker_editor {

namespace {

AddonAPI* api = nullptr;
NexusLinkData* nexus_link = nullptr;
Mumble::Data* mumble_link = nullptr;

bool show_window = true;

// Absolute path of the pack currently open, empty when none is.
std::filesystem::path open_pack_path;
std::string status_message;

editor::State state;

// Fraction of the window width given to the tree.
float tree_split = 0.42f;

void log(ELogLevel level, const std::string& message) {
    api->Log(level, ADDON_NAME, message.c_str());
}

// <Documents>/Guild Wars 2/addons/blishhud/markers — where Blish HUD's Pathing module
// looks for packs, and so the sensible place to start browsing. Resolved rather than
// hardcoded so this works on anyone's machine.
std::filesystem::path blishhudMarkersFolder() {
    PWSTR documents = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) return {};
    std::filesystem::path path = std::filesystem::path(documents) / "Guild Wars 2" / "addons" / "blishhud" / "markers";
    CoTaskMemFree(documents);
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) return {};
    return path;
}

void openPack(const std::filesystem::path& path) {
    // TODO: move off the render thread once a pack is big enough to stutter. The largest
    // pack tested (56k markers) loads fast enough that it has not been worth it yet.
    state.reset();

    pack::zip::Archive archive;
    std::string error;
    if (!archive.open(path, &error)) {
        status_message = "Could not open " + path.filename().string() + ": " + error;
        log(ELogLevel_WARNING, status_message);
        return;
    }

    if (!pack::loadPack(archive, &state.pack, &error)) {
        status_message = "Could not read " + path.filename().string() + ": " + error;
        log(ELogLevel_WARNING, status_message);
        return;
    }

    state.rebuildIndex();
    state.loaded = true;
    open_pack_path = path;

    status_message = std::to_string(state.pack.categories.size()) + " categories, " +
                     std::to_string(state.pack.countByType(pack::PathableType::Poi)) + " POIs, " +
                     std::to_string(state.pack.countByType(pack::PathableType::Trail)) + " trails";
    if (!state.pack.warnings.empty()) {
        status_message += ", " + std::to_string(state.pack.warnings.size()) + " warnings";
        for (const pack::Warning& warning : state.pack.warnings) {
            log(ELogLevel_WARNING, warning.file + ": " + warning.message);
        }
    }
    log(ELogLevel_INFO, "Opened " + path.filename().string() + " — " + status_message);
}

void render() {
    if (std::optional<std::filesystem::path> picked = file_dialog::takeResult()) {
        openPack(*picked);
    }

    if (!show_window) return;

    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Marker Pack Editor##MarkerPackEditor", &show_window)) {
        // Nexus ships imgui 1.80, which predates BeginDisabled/EndDisabled.
        if (file_dialog::isPending()) {
            ImGui::TextDisabled("Open Pack... (dialog open)");
        } else if (ImGui::Button("Open Pack...")) {
            file_dialog::requestOpenFile(blishhudMarkersFolder(), {{"Marker pack", "zip,taco"}});
        }

        if (state.loaded) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s%s", open_pack_path.filename().string().c_str(), state.dirty ? " *" : "");
        }

        if (!status_message.empty()) ImGui::TextWrapped("%s", status_message.c_str());
        ImGui::Separator();

        const float available = ImGui::GetContentRegionAvail().x;
        const float tree_width = available * tree_split;

        ImGui::BeginChild("##left", ImVec2(tree_width, 0), true);
        ui::tree_window::draw(state);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##right", ImVec2(0, 0), true);
        ui::property_panel::draw(state);
        ImGui::EndChild();
    }
    ImGui::End();
}

void drawOptions() {
    ImGui::TextDisabled("No options yet.");
}

void drawQuickAccess() {
    if (ImGui::MenuItem("Marker Pack Editor")) show_window = !show_window;
}

void onToggleKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease) return;
    show_window = !show_window;
}

} // namespace

void load(AddonAPI* _api, NexusLinkData* _nexus_link, Mumble::Data* _mumble_link) {
    api = _api;
    nexus_link = _nexus_link;
    mumble_link = _mumble_link;

    api->Renderer.Register(ERenderType_Render, render);
    api->Renderer.Register(ERenderType_OptionsRender, drawOptions);
    // nullptr target -> appended to the Nexus main menu.
    api->QuickAccess.AddContextMenu(QUICKACCESS_ID, nullptr, drawQuickAccess);
    // "(null)" -> registered without a default bind; the user sets it in Nexus settings.
    api->InputBinds.RegisterWithString(KEYBIND_TOGGLE_ID, onToggleKeybind, "(null)");
    api->Localization.Set(KEYBIND_TOGGLE_ID, "en", "Toggle marker pack editor");
}

void unload() {
    api->Renderer.Deregister(render);
    api->Renderer.Deregister(drawOptions);
    api->QuickAccess.RemoveContextMenu(QUICKACCESS_ID);
    api->InputBinds.Deregister(KEYBIND_TOGGLE_ID);
    file_dialog::shutdown();
}

} // namespace marker_editor
