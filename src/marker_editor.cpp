#include "marker_editor.h"

#include <ShlObj.h>
#include <imgui.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>

#include "editor_state.h"
#include "file_dialog.h"
#include "pack/source.h"
#include "pack/writer.h"
#include "pack/xml_reader.h"
#include "pathable_edit.h"
#include "resources.h"
#include "trail_edit.h"
#include "ui/property_panel.h"
#include "ui/resource_window.h"
#include "ui/trail_editor.h"
#include "ui/tree_window.h"
#include "ui/windows.h"
#include "undo.h"
#include "world/camera.h"
#include "world/gizmo.h"
#include "world/renderer.h"

namespace marker_editor {

namespace {

AddonAPI* api = nullptr;
NexusLinkData* nexus_link = nullptr;
Mumble::Data* mumble_link = nullptr;

// Absolute path of the pack currently open, empty when none is.
std::filesystem::path open_pack_path;
std::string status_message;

// Where the folder browser starts next time. A pack repo rarely lives under the Blish
// HUD markers folder, so defaulting there every time would be a nuisance.
std::filesystem::path last_folder;

// Likewise for adding images: icons are authored wherever the user keeps them, and coming
// back to the same place beats browsing from scratch for each one.
std::filesystem::path last_image_folder;

editor::State state;

// This frame's camera, kept so the menu bar can reach the player position and map id
// without every window that draws the bar having to be handed a camera.
world::Camera frame_camera;

// A world click picked this pathable; the render thread consumes it. Written from the
// game's message thread — see onWndProc for why the click cannot come from ImGui.
std::atomic<size_t> pending_select{pack::kNoIndex};
// The same for a point of the trail open in the editor. Which trail it belongs to is not
// carried across: the render thread owns that and reads it when it consumes this.
std::atomic<size_t> pending_select_point{pack::kNoIndex};
std::atomic<bool> pending_select_additive{false};

// What the in-flight file dialog is for. One dialog runs at a time, so a single value is
// enough, and it has to survive until the worker reports back.
enum class DialogPurpose { OpenFile, OpenFolder, SaveAs, AddResource, ExportTrail };
DialogPurpose dialog_purpose = DialogPurpose::OpenFile;

// Close and Reload both throw away unsaved edits, so with a dirty pack they go through a
// confirmation first rather than doing it silently.
//
// The confirmation belongs to whichever window's menu asked for it. An ImGui popup is
// scoped to the window that opened it, and the same menu bar is now drawn in every large
// window, so without an owner three windows would fight over one popup id.
enum class PendingAction { None, Close, Reload };
PendingAction pending_action = PendingAction::None;
bool confirm_pending = false; // the action is waiting on the modal
bool open_confirm_popup = false; // OpenPopup on the owner's next frame
ui::windows::Id confirm_owner = ui::windows::Id::Tree;

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
    // Its pathable index and its working copy both belong to the pack going away, and so
    // does every undo record.
    trail_edit::close();
    undo::clear();

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

    // A trail point handle wins over the marker behind it: the handles are small, only the
    // open trail has them, and they are drawn precisely to be aimed at.
    const size_t point = world::renderer::hoveredTrailPoint();
    const size_t hovered = world::renderer::hoveredPathable();
    if (point == pack::kNoIndex && hovered == pack::kNoIndex) return 1;

    // Shift does what ctrl does out here. A range needs an order between two markers, and
    // the world has none — the tree is where a range means something.
    pending_select_additive.store((wParam & (MK_CONTROL | MK_SHIFT)) != 0);
    if (point != pack::kNoIndex) {
        pending_select_point.store(point);
    } else {
        pending_select.store(hovered);
    }
    return 0;
}

// " (+2 images, -1 removed)", or nothing at all when no asset changed.
std::string assetSummary(const pack::SaveResult& result) {
    if (result.assets_added == 0 && result.assets_removed == 0) return {};

    std::string summary = " (";
    if (result.assets_added > 0) summary += "+" + std::to_string(result.assets_added) + " images";
    if (result.assets_added > 0 && result.assets_removed > 0) summary += ", ";
    if (result.assets_removed > 0) summary += "-" + std::to_string(result.assets_removed) + " removed";
    return summary + ")";
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
        for (const std::string& warning : result.warnings) log(ELogLevel_WARNING, warning);

        // They are on disk now, so they are no longer pending.
        state.pack.clearAssetChanges();
        state.dirty = false;
        status_message =
            "Saved " + std::to_string(result.entries_written) + " changed file(s)" + assetSummary(result) + " to " + target.string();
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
        // The new archive holds them now; keeping them pending would write them again.
        state.pack.clearAssetChanges();
        state.dirty = false;
        open_pack_path = target;
        status_message = "Saved " + std::to_string(result.entries_written) + " entries (" + std::to_string(result.files_rewritten) +
                         " xml rewritten)" + assetSummary(result) + " to " + target.filename().string();
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
    trail_edit::close();
    undo::clear();
    resources::setSource(nullptr);
    open_pack_path.clear();
    status_message.clear();
}

// Undo can change the bytes behind the open trail without going through the trail editor,
// so the working copy has to be told to reread.
void applyUndo() {
    undo::undo(state);
    trail_edit::refresh(state);
}

void applyRedo() {
    undo::redo(state);
    trail_edit::refresh(state);
}

void requestAction(PendingAction action, ui::windows::Id owner) {
    pending_action = action;
    confirm_owner = owner;
    // Nothing to lose with a clean pack: it goes ahead at the end of the frame.
    confirm_pending = state.dirty;
    open_confirm_popup = state.dirty;
}

// The same bar in every large window, so File and View are reachable from whichever one
// the user kept open. `owner` is the window drawing it — see the confirmation popup.
void drawMenuBar(ui::windows::Id owner) {
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

        if (ImGui::MenuItem("Reload", nullptr, false, state.loaded)) requestAction(PendingAction::Reload, owner);
        if (ImGui::MenuItem("Close", nullptr, false, state.loaded)) requestAction(PendingAction::Close, owner);

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

    if (ImGui::BeginMenu("Edit")) {
        const std::string undo_label = undo::canUndo() ? "Undo " + undo::undoLabel() : std::string("Undo");
        const std::string redo_label = undo::canRedo() ? "Redo " + undo::redoLabel() : std::string("Redo");

        if (ImGui::MenuItem(undo_label.c_str(), "Ctrl+Z", false, undo::canUndo())) applyUndo();
        if (ImGui::MenuItem(redo_label.c_str(), "Ctrl+Y", false, undo::canRedo())) applyRedo();

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ui::windows::drawViewMenu();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Settings")) {
        ImGui::MenuItem("3D highlight", nullptr, &state.settings.highlight_3d);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Outline the selection in the world.");

        ImGui::MenuItem("Select in viewport", nullptr, &state.settings.select_in_viewport);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click a marker in the world to select it.");

        ImGui::EndMenu();
    }

    // Last, and only where it belongs: the Trail menu acts on the trail editor's own
    // state, so it has no business hanging off the tree or the details window.
    if (owner == ui::windows::Id::TrailEditor) {
        if (ui::trail_editor::drawMenu(state, frame_camera) == ui::trail_editor::Request::Export && !file_dialog::isPending()) {
            dialog_purpose = DialogPurpose::ExportTrail;
            const std::filesystem::path folder = last_folder.empty() ? blishhudMarkersFolder() : last_folder;
            file_dialog::requestSaveFile(folder, "trail.trl", {{"Trail data", "trl"}});
        }
    }

    ImGui::EndMenuBar();
}

// Deferred to the end of the frame: closing or reloading frees the vectors every window
// has just drawn from, and a menu click happens partway through that.
void applyPendingAction() {
    if (confirm_pending) {
        // The window carrying the modal was closed out from under it, taking the popup
        // with it. Drop the action rather than leaving it armed for the next frame.
        if (!ui::windows::visible(confirm_owner)) {
            confirm_pending = false;
            pending_action = PendingAction::None;
        }
        return;
    }

    switch (pending_action) {
    case PendingAction::Close: closePack(); break;
    case PendingAction::Reload: openPack(open_pack_path); break;
    case PendingAction::None: break;
    }
    pending_action = PendingAction::None;
}

// Runs after the menu bar, in the window that owns the request: a popup cannot be opened
// from inside a menu, and it has to be begun from the same window every frame.
void drawConfirmPopup(ui::windows::Id owner) {
    if (!confirm_pending || owner != confirm_owner) return;

    constexpr const char* kTitle = "Discard changes?";

    if (open_confirm_popup) {
        ImGui::OpenPopup(kTitle);
        open_confirm_popup = false;
    }

    if (ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This pack has unsaved changes.");
        ImGui::TextDisabled("%s", pending_action == PendingAction::Reload ? "Reloading will discard them." : "Closing will discard them.");
        ImGui::Separator();

        if (ImGui::Button("Discard", ImVec2(110, 0))) {
            confirm_pending = false; // applied at the end of the frame
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save first", ImVec2(110, 0))) {
            savePack();
            if (state.dirty) pending_action = PendingAction::None; // the save failed, so keep the pack
            confirm_pending = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0))) {
            pending_action = PendingAction::None;
            confirm_pending = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ── Windows ───────────────────────────────────────────────────────────────────

// Begins one editor window and its menu bar. Returns false when the window is hidden or
// collapsed, in which case it has already been ended and the caller draws nothing;
// otherwise the caller draws its contents and calls ImGui::End itself.
bool beginWindow(ui::windows::Id id, const ImVec2& first_size, bool with_menus) {
    if (!ui::windows::visible(id)) return false;

    ImGui::SetNextWindowSize(first_size, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ui::windows::title(id), &ui::windows::visible(id), with_menus ? ImGuiWindowFlags_MenuBar : 0)) {
        ImGui::End();
        return false;
    }

    if (with_menus) {
        drawMenuBar(id);
        drawConfirmPopup(id);
    }
    return true;
}

void drawPackHeader() {
    if (file_dialog::isPending()) {
        ImGui::TextDisabled("(file dialog open)");
    } else if (state.loaded) {
        const std::string name = state.pack.from_directory ? open_pack_path.filename().string() + "/" : open_pack_path.filename().string();
        ImGui::TextDisabled("%s%s", name.c_str(), state.dirty ? " *" : "");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", open_pack_path.string().c_str());
    } else {
        ImGui::TextDisabled("No pack open. Use File > Open Pack.");
    }

    if (!status_message.empty()) ImGui::TextWrapped("%s", status_message.c_str());
    ImGui::Separator();
}

void drawTreeWindow() {
    if (!beginWindow(ui::windows::Id::Tree, ImVec2(360, 560), true)) return;
    drawPackHeader();
    ui::tree_window::draw(state);
    ImGui::End();
}

void drawDetailsWindow() {
    if (!beginWindow(ui::windows::Id::Details, ImVec2(420, 560), true)) return;
    ui::property_panel::draw(state);
    ImGui::End();
}

// Adds a marker where the character is standing. Placement follows the selection, so the
// button needs no target of its own — see pathable_edit::addPoi.
void drawAddPoi() {
    if (!ImGui::Button("Add POI")) {
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Adds a marker at the character's position.\n"
                "With a category selected it goes in at the end;\n"
                "with a marker selected it follows it and copies its attributes.");
        }
        return;
    }

    if (!state.loaded) {
        status_message = "Open a pack first.";
        return;
    }
    if (!frame_camera.valid) {
        status_message = "No character position: MumbleLink has no data yet.";
        return;
    }

    const size_t added = pathable_edit::addPoi(state, frame_camera.player);
    if (added == pack::kNoIndex) {
        status_message = "Select a category or a marker to say where the POI goes.";
    } else {
        status_message = "Added a POI at the character's position.";
    }
}

// No menu bar: this one is a strip of controls, and a menu bar over it would be taller
// than its contents.
void drawToolbarWindow() {
    if (!beginWindow(ui::windows::Id::Toolbar, ImVec2(430, 80), false)) return;

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

    ImGui::Separator();
    drawAddPoi();

    ImGui::End();
}

void drawTrailEditorWindow() {
    if (!beginWindow(ui::windows::Id::TrailEditor, ImVec2(420, 560), true)) return;
    ui::trail_editor::draw(state, frame_camera);
    ImGui::End();
}

void drawResourcesWindow() {
    if (!beginWindow(ui::windows::Id::Resources, ImVec2(420, 620), true)) return;

    // The window cannot open the dialog itself: one dialog runs at a time and its result is
    // polled here, so the request comes back out and is issued from the one place that
    // knows what the in-flight dialog is for.
    if (ui::resource_window::draw(state) && !file_dialog::isPending()) {
        dialog_purpose = DialogPurpose::AddResource;
        file_dialog::requestOpenFile(last_image_folder, {{"Image", "png,jpg,jpeg,bmp"}});
    }

    ImGui::End();
}

void render() {
    if (std::optional<std::filesystem::path> picked = file_dialog::takeResult()) {
        switch (dialog_purpose) {
        case DialogPurpose::SaveAs:
            // Save As always produces a zip, even from a folder pack: that is the only
            // form Blish HUD loads reliably.
            savePackTo(*picked, false);
            break;
        case DialogPurpose::AddResource:
            last_image_folder = picked->parent_path();
            ui::resource_window::addFile(state, *picked, &status_message);
            break;
        case DialogPurpose::ExportTrail: ui::trail_editor::exportTo(state, *picked, &status_message); break;
        default: openPack(*picked); break;
        }
    }

    resources::beginFrame();
    undo::beginFrame();

    // The world is drawn whether or not any editor window is open — the point is to see
    // the pack in the game.
    const world::Camera camera = world::fromMumble(mumble_link, nexus_link);
    frame_camera = camera;
    world::renderer::draw(state, camera);

    // After the markers so it draws over them, and so it can claim the click first.
    const bool gizmo_active = world::gizmo::draw(state, camera);

    const size_t picked = pending_select.exchange(pack::kNoIndex);
    const size_t picked_point = pending_select_point.exchange(pack::kNoIndex);

    if (!gizmo_active && picked_point != pack::kNoIndex && picked_point < trail_edit::pointCount()) {
        const editor::NodeRef node{editor::NodeKind::TrailPoint, trail_edit::pathable(), picked_point};
        if (pending_select_additive.load()) {
            state.selection.toggle(node);
        } else {
            state.selection.set(node);
        }
    } else if (picked != pack::kNoIndex && !gizmo_active && picked < state.pack.pathables.size()) {
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

    drawTreeWindow();
    drawDetailsWindow();
    drawToolbarWindow();
    drawTrailEditorWindow();
    drawResourcesWindow();

    applyPendingAction();
}

void drawOptions() {
    // The way back once a window's close button has been used, short of the keybind or the
    // quick access menu — neither of which is bound by default.
    ImGui::TextDisabled("Windows");
    ui::windows::drawOptions();
    ImGui::TextDisabled("The keybind and the Nexus menu item hide and restore all of them at once.");
}

void drawQuickAccess() {
    if (ImGui::MenuItem("Marker Pack Editor")) ui::windows::toggleAll();
}

void onToggleKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease) return;
    ui::windows::toggleAll();
}

// Nexus binds are global, so they fire while a text field has the caret too — where Ctrl+Z
// means "undo my typing" to ImGui and would mean "undo an edit" here. The field wins.
void onEditKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease || !state.loaded) return;
    if (ImGui::GetIO().WantTextInput) return;

    if (std::strcmp(aIdentifier, KEYBIND_UNDO_ID) == 0) {
        applyUndo();
    } else if (std::strcmp(aIdentifier, KEYBIND_REDO_ID) == 0) {
        applyRedo();
    }
}

} // namespace

void load(AddonAPI* _api, NexusLinkData* _nexus_link, Mumble::Data* _mumble_link) {
    api = _api;
    nexus_link = _nexus_link;
    mumble_link = _mumble_link;

    resources::init(api);
    // Images added in the editor are not in the DataSource until a save, so the cache has
    // to be able to see the pack's pending adds. `state` outlives every load.
    resources::setPendingAssets(&state.pack);

    api->Renderer.Register(ERenderType_Render, render);
    api->Renderer.Register(ERenderType_OptionsRender, drawOptions);
    // nullptr target -> appended to the Nexus main menu.
    api->QuickAccess.AddContextMenu(QUICKACCESS_ID, nullptr, drawQuickAccess);
    // "(null)" -> registered without a default bind; the user sets it in Nexus settings.
    api->InputBinds.RegisterWithString(KEYBIND_TOGGLE_ID, onToggleKeybind, "(null)");
    // These two do come with a default: they are the bindings everyone already has in their
    // fingers, and an editor without them is a nuisance out of the box.
    api->InputBinds.RegisterWithString(KEYBIND_UNDO_ID, onEditKeybind, "CTRL+Z");
    api->InputBinds.RegisterWithString(KEYBIND_REDO_ID, onEditKeybind, "CTRL+Y");
    api->WndProc.Register(onWndProc);
    api->Localization.Set(KEYBIND_TOGGLE_ID, "en", "Toggle marker pack editor");
    api->Localization.Set(KEYBIND_UNDO_ID, "en", "Undo");
    api->Localization.Set(KEYBIND_REDO_ID, "en", "Redo");
}

void unload() {
    api->Renderer.Deregister(render);
    api->Renderer.Deregister(drawOptions);
    api->QuickAccess.RemoveContextMenu(QUICKACCESS_ID);
    api->InputBinds.Deregister(KEYBIND_TOGGLE_ID);
    api->InputBinds.Deregister(KEYBIND_UNDO_ID);
    api->InputBinds.Deregister(KEYBIND_REDO_ID);
    api->WndProc.Deregister(onWndProc);
    file_dialog::shutdown();
    resources::shutdown();
}

} // namespace marker_editor
