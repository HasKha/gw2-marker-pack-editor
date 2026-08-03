#include "marker_editor.h"

#include <ShlObj.h>
#include <imgui.h>

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>

#include "editor_state.h"
#include "file_dialog.h"
#include "pack/source.h"
#include "pack/writer.h"
#include "pack/xml_reader.h"
#include "resources.h"
#include "ui/property_panel.h"
#include "ui/tree_window.h"
#include "world/camera.h"
#include "world/gizmo.h"
#include "world/renderer.h"

namespace marker_editor {

namespace {

AddonAPI* api = nullptr;
NexusLinkData* nexus_link = nullptr;
Mumble::Data* mumble_link = nullptr;

bool show_window = true;

// Absolute path of the pack currently open, empty when none is.
std::filesystem::path open_pack_path;
std::string status_message;

// Where the folder browser starts next time. A pack repo rarely lives under the Blish
// HUD markers folder, so defaulting there every time would be a nuisance.
std::filesystem::path last_folder;

editor::State state;

// Fraction of the window width given to the tree.
float tree_split = 0.42f;

// A world click picked this pathable; the render thread consumes it. Written from the
// game's message thread — see onWndProc for why the click cannot come from ImGui.
std::atomic<size_t> pending_select{pack::kNoIndex};
std::atomic<bool> pending_select_additive{false};

// What the in-flight file dialog is for. One dialog runs at a time, so a single value is
// enough, and it has to survive until the worker reports back.
enum class DialogPurpose { OpenFile, OpenFolder, SaveAs };
DialogPurpose dialog_purpose = DialogPurpose::OpenFile;

// Close and Reload both throw away unsaved edits, so with a dirty pack they go through a
// confirmation first rather than doing it silently.
enum class PendingAction { None, Close, Reload };
PendingAction pending_action = PendingAction::None;
bool open_confirm_popup = false;

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

// By value on purpose: Reload passes open_pack_path, which this function reassigns
// partway through. A reference would alias it and change under us.
void openPack(std::filesystem::path path) {
    // TODO: move off the render thread once a pack is big enough to stutter. The largest
    // pack tested (56k markers) loads fast enough that it has not been worth it yet.
    state.reset();

    // Zip or folder — pack::open picks by what the path actually is.
    std::string error;
    std::unique_ptr<pack::DataSource> source = pack::open(path, &error);
    if (!source) {
        status_message = "Could not open " + path.filename().string() + ": " + error;
        log(ELogLevel_WARNING, status_message);
        return;
    }

    if (!pack::loadPack(*source, &state.pack, &error)) {
        status_message = "Could not read " + path.filename().string() + ": " + error;
        log(ELogLevel_WARNING, status_message);
        return;
    }

    // The renderer reads icons and .trl data straight out of the source as markers become
    // visible, so it has to outlive the load.
    resources::setSource(std::move(source));

    state.rebuildIndex();
    state.loaded = true;
    open_pack_path = state.pack.source_path;
    if (state.pack.from_directory) last_folder = state.pack.source_path;

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

// True only when the OS cursor is actually on screen. Nexus stops updating ImGui's
// MousePos while the cursor is hidden (UiInput.cpp), so during mouselook the last known
// position lingers and would otherwise let a camera drag "click" whatever it was last
// pointing at.
bool isCursorVisible() {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info)) return true;
    return (info.flags & CURSOR_SHOWING) != 0;
}

// Selecting a marker in the world cannot go through ImGui. Nexus only forwards a click to
// ImGui when io.WantCaptureMouse is already true:
//
//     case WM_LBUTTONDOWN:
//         if (io.WantCaptureMouse && !Inputs::IsCursorHidden()) { io.MouseDown[0] = true; ... }
//
// so a click on the world — which is exactly the case where WantCaptureMouse is false —
// never reaches io.MouseDown at all and ImGui::IsMouseClicked can never fire for it. The
// message has to be taken from Nexus's WndProc chain instead.
//
// Returning 0 consumes the message (RiApi.cpp), which we only do when a marker was
// actually hit, so ordinary clicks still drive the camera.
UINT onWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg != WM_LBUTTONDOWN) return 1;
    if (!state.loaded || !state.settings.select_in_viewport) return 1;
    if (ImGui::GetIO().WantCaptureMouse) return 1; // an addon window owns this click
    if (!isCursorVisible()) return 1;

    const size_t hovered = world::renderer::hoveredPathable();
    if (hovered == pack::kNoIndex) return 1;

    // Shift does what ctrl does out here. A range needs an order between two markers, and
    // the world has none — the tree is where a range means something.
    pending_select_additive.store((wParam & (MK_CONTROL | MK_SHIFT)) != 0);
    pending_select.store(hovered);
    return 0;
}

// Writes the pack back to `target`. A folder pack updates its changed .xml files in place;
// a zip is rebuilt whole, since every other entry has to come along with it.
void savePackTo(const std::filesystem::path& target, bool as_directory) {
    if (!state.loaded) return;

    if (as_directory) {
        const pack::SaveResult result = pack::saveToDirectory(state.pack, target);
        if (!result.ok) {
            status_message = "Save failed: " + result.error;
            log(ELogLevel_WARNING, status_message);
            return;
        }
        state.dirty = false;
        status_message = "Saved " + std::to_string(result.entries_written) + " changed file(s) to " + target.string();
        log(ELogLevel_INFO, status_message);
        return;
    }

    const pack::DataSource* source = resources::source();
    if (!source) {
        status_message = "Save failed: the pack's source is no longer open";
        log(ELogLevel_WARNING, status_message);
        return;
    }

    const pack::SaveResult result = pack::buildZip(state.pack, *source, target);
    if (!result.ok) {
        status_message = "Save failed: " + result.error;
        log(ELogLevel_WARNING, status_message);
        pack::abortZip(target);
        return;
    }
    for (const std::string& warning : result.warnings) log(ELogLevel_WARNING, warning);

    // The archive we are about to replace is the one we still have open for reading, and
    // Windows will not rename over a live handle. Drop it, swap, then reopen.
    resources::setSource(nullptr);

    std::string error;
    if (!pack::commitZip(target, &error)) {
        status_message = "Save failed: " + error;
        log(ELogLevel_WARNING, status_message);
        pack::abortZip(target);
    } else {
        state.dirty = false;
        open_pack_path = target;
        status_message = "Saved " + std::to_string(result.entries_written) + " entries (" + std::to_string(result.files_rewritten) +
                         " xml rewritten) to " + target.filename().string();
        log(ELogLevel_INFO, status_message);
    }

    // Reopen either way: without a source the icons and trails stop resolving.
    std::unique_ptr<pack::DataSource> reopened = pack::open(open_pack_path, &error);
    if (reopened) {
        resources::setSource(std::move(reopened));
    } else {
        log(ELogLevel_WARNING, "Could not reopen the pack after saving: " + error);
    }
}

void savePack() {
    if (!state.loaded) return;
    savePackTo(open_pack_path, state.pack.from_directory);
}

void closePack() {
    state.reset();
    resources::setSource(nullptr);
    open_pack_path.clear();
    status_message.clear();
}

void drawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Pack...", nullptr, false, !file_dialog::isPending())) {
            dialog_purpose = DialogPurpose::OpenFile;
            file_dialog::requestOpenFile(blishhudMarkersFolder(), {{"Marker pack", "zip,taco"}});
        }
        if (ImGui::MenuItem("Open Folder...", nullptr, false, !file_dialog::isPending())) {
            dialog_purpose = DialogPurpose::OpenFolder;
            file_dialog::requestOpenFolder(last_folder.empty() ? blishhudMarkersFolder() : last_folder);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reload", nullptr, false, state.loaded)) {
            pending_action = PendingAction::Reload;
            open_confirm_popup = state.dirty;
        }
        if (ImGui::MenuItem("Close", nullptr, false, state.loaded)) {
            pending_action = PendingAction::Close;
            open_confirm_popup = state.dirty;
        }

        if (ImGui::MenuItem("Save", nullptr, false, state.loaded && state.dirty)) {
            savePack();
        }
        if (ImGui::IsItemHovered() && state.loaded) {
            ImGui::SetTooltip(
                state.pack.from_directory ? "Rewrites the changed .xml files in place."
                                          : "Rebuilds the archive. The previous one is kept as .bak.");
        }

        if (ImGui::MenuItem("Save As...", nullptr, false, state.loaded && !file_dialog::isPending())) {
            dialog_purpose = DialogPurpose::SaveAs;
            const std::string suggested =
                open_pack_path.empty() ? std::string("markers.zip") : open_pack_path.filename().replace_extension(".zip").string();
            file_dialog::requestSaveFile(blishhudMarkersFolder(), suggested, {{"Marker pack", "zip,taco"}});
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Always writes a zip — the only form Blish HUD loads reliably.");

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Settings")) {
        ImGui::MenuItem("3D highlight", nullptr, &state.settings.highlight_3d);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Outline the selection in the world.");

        ImGui::MenuItem("Select in viewport", nullptr, &state.settings.select_in_viewport);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click a marker in the world to select it.");

        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

void applyPendingAction() {
    switch (pending_action) {
    case PendingAction::Close: closePack(); break;
    case PendingAction::Reload: openPack(open_pack_path); break;
    case PendingAction::None: break;
    }
    pending_action = PendingAction::None;
}

// Runs after the menu bar: a popup cannot be opened from inside a menu, and the action is
// deferred anyway so the menu is closed by the time anything happens.
void drawConfirmPopup() {
    constexpr const char* kTitle = "Discard changes?";

    if (open_confirm_popup) {
        ImGui::OpenPopup(kTitle);
        open_confirm_popup = false;
    } else if (pending_action != PendingAction::None) {
        applyPendingAction(); // nothing to lose, go straight ahead
    }

    if (ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This pack has unsaved changes.");
        ImGui::TextDisabled("%s", pending_action == PendingAction::Reload ? "Reloading will discard them." : "Closing will discard them.");
        ImGui::Separator();

        if (ImGui::Button("Discard", ImVec2(110, 0))) {
            applyPendingAction();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save first", ImVec2(110, 0))) {
            savePack();
            if (!state.dirty) applyPendingAction(); // only proceed if the save worked
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0))) {
            pending_action = PendingAction::None;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void drawGizmoControls() {
    ImGui::TextUnformatted("Gizmo");
    ImGui::SameLine();

    int mode = static_cast<int>(state.settings.gizmo);
    ImGui::RadioButton("Disabled", &mode, static_cast<int>(editor::GizmoMode::Disabled));
    ImGui::SameLine();
    ImGui::RadioButton("Translate", &mode, static_cast<int>(editor::GizmoMode::Translate));
    ImGui::SameLine();
    ImGui::RadioButton("Rotate", &mode, static_cast<int>(editor::GizmoMode::Rotate));
    ImGui::SameLine();
    ImGui::RadioButton("Both", &mode, static_cast<int>(editor::GizmoMode::TranslateRotate));
    state.settings.gizmo = static_cast<editor::GizmoMode>(mode);
}

void render() {
    if (std::optional<std::filesystem::path> picked = file_dialog::takeResult()) {
        if (dialog_purpose == DialogPurpose::SaveAs) {
            // Save As always produces a zip, even from a folder pack: that is the only
            // form Blish HUD loads reliably.
            savePackTo(*picked, false);
        } else {
            openPack(*picked);
        }
    }

    resources::beginFrame();

    // The world is drawn whether or not the editor window is open — the point is to see
    // the pack in the game.
    const world::Camera camera = world::fromMumble(mumble_link, nexus_link);
    world::renderer::draw(state, camera);

    // After the markers so it draws over them, and so it can claim the click first.
    const bool gizmo_active = world::gizmo::draw(state, camera);

    const size_t picked = pending_select.exchange(pack::kNoIndex);
    if (picked != pack::kNoIndex && !gizmo_active && picked < state.pack.pathables.size()) {
        const editor::NodeRef node{editor::NodeKind::Pathable, picked};
        if (pending_select_additive.load()) {
            state.selection.toggle(node);
        } else {
            state.selection.set(node);
        }
        // Picking in the world is only half the job: the marker has to be findable in the
        // tree afterwards, which for a deep category means opening the way down to it.
        if (state.selection.contains(node)) state.requestReveal(node);
    }

    if (!show_window) return;

    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Marker Pack Editor##MarkerPackEditor", &show_window, ImGuiWindowFlags_MenuBar)) {
        drawMenuBar();
        drawConfirmPopup();
        drawGizmoControls();

        if (file_dialog::isPending()) {
            ImGui::TextDisabled("(file dialog open)");
        } else if (state.loaded) {
            const std::string name =
                state.pack.from_directory ? open_pack_path.filename().string() + "/" : open_pack_path.filename().string();
            ImGui::TextDisabled("%s%s", name.c_str(), state.dirty ? " *" : "");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", open_pack_path.string().c_str());
        } else {
            ImGui::TextDisabled("No pack open. Use File > Open Pack.");
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
    // The only way back once the window's close button has been used, short of the
    // keybind or the quick access menu — neither of which is bound by default.
    ImGui::Checkbox("Show Marker Pack Editor window", &show_window);
    ImGui::TextDisabled("Also toggled by the keybind and the Nexus menu item.");
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

    resources::init(api);

    api->Renderer.Register(ERenderType_Render, render);
    api->Renderer.Register(ERenderType_OptionsRender, drawOptions);
    // nullptr target -> appended to the Nexus main menu.
    api->QuickAccess.AddContextMenu(QUICKACCESS_ID, nullptr, drawQuickAccess);
    // "(null)" -> registered without a default bind; the user sets it in Nexus settings.
    api->InputBinds.RegisterWithString(KEYBIND_TOGGLE_ID, onToggleKeybind, "(null)");
    api->WndProc.Register(onWndProc);
    api->Localization.Set(KEYBIND_TOGGLE_ID, "en", "Toggle marker pack editor");
}

void unload() {
    api->Renderer.Deregister(render);
    api->Renderer.Deregister(drawOptions);
    api->QuickAccess.RemoveContextMenu(QUICKACCESS_ID);
    api->InputBinds.Deregister(KEYBIND_TOGGLE_ID);
    api->WndProc.Deregister(onWndProc);
    file_dialog::shutdown();
    resources::shutdown();
}

} // namespace marker_editor
