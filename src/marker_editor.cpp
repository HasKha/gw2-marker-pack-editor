#include "marker_editor.h"

#include <ShlObj.h>
#include <imgui.h>

#include <filesystem>
#include <optional>
#include <string>

#include "file_dialog.h"

namespace marker_editor {

namespace {

AddonAPI* api = nullptr;
NexusLinkData* nexus_link = nullptr;
Mumble::Data* mumble_link = nullptr;

bool show_window = true;

// Absolute path of the pack currently open, empty when none is.
std::filesystem::path open_pack_path;
std::string status_message;

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
    // TODO(milestone 1): read the zip, parse the XML files, build the pack model.
    open_pack_path = path;
    status_message = "Selected " + path.filename().string() + " (loading not implemented yet)";
    log(ELogLevel_INFO, status_message);
}

void render() {
    if (std::optional<std::filesystem::path> picked = file_dialog::takeResult()) {
        openPack(*picked);
    }

    if (!show_window) return;

    if (ImGui::Begin("Marker Pack Editor##MarkerPackEditor", &show_window)) {
        // Nexus ships imgui 1.80, which predates BeginDisabled/EndDisabled.
        if (file_dialog::isPending()) {
            ImGui::TextDisabled("Open Pack... (dialog open)");
        } else if (ImGui::Button("Open Pack...")) {
            file_dialog::requestOpenFile(blishhudMarkersFolder(), {{"Marker pack", "zip,taco"}});
        }

        ImGui::Separator();
        if (open_pack_path.empty()) {
            ImGui::TextDisabled("No pack open.");
        } else {
            ImGui::TextUnformatted(open_pack_path.string().c_str());
        }
        if (!status_message.empty()) ImGui::TextWrapped("%s", status_message.c_str());
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
