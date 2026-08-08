#include "marker_editor.h"

#include <ShlObj.h>
#include <imgui.h>

#include <atomic>
#include <cstdlib>
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
#include "settings.h"
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

// A world click that hit nothing wants to clear the selection — but only if it was a click
// and not the start of a camera drag, which cannot be known until the button comes back up.
// See onWndProc.
std::atomic<bool> pending_deselect{false};
bool deselect_armed = false; // message thread only
POINT deselect_origin{};

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
enum class PendingAction { None, Close, Reload, NewPack };
PendingAction pending_action = PendingAction::None;
bool confirm_pending = false; // the action is waiting on the modal
bool open_confirm_popup = false; // OpenPopup on the owner's next frame
ui::windows::Id confirm_owner = ui::windows::Id::Tree;

// New Pack asks for a few details first, in a popup owned the same way.
bool open_new_pack_popup = false;
char new_pack_name[64] = "mypack";
char new_pack_display[96] = "My Pack";
char new_pack_file[64] = "markers.xml";

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
// Clicking nothing clears the selection, which is the only way to clear it out here. The
// catch is that a left *drag* on nothing is how the camera turns, and a drag begins with the
// same WM_LBUTTONDOWN — so the deselect cannot be decided until the button comes back up.
//
// Telling the two apart by comparing the down and up positions does not work. GW2 hides the
// cursor while it turns the camera, and Nexus's `MouseResetFix` optionally clips it to a 1x1
// rect and `SetCursorPos`es it back to where it went down, so a long drag can report an up
// position identical to its down position. The cursor going hidden is the reliable signal,
// and it is the game telling us it has taken the mouse for mouselook. Movement past a few
// pixels is checked too, for a drag where the cursor stays visible.
//
// Returns 1 throughout: the game has to keep seeing these, or the camera stops working.
UINT trackDeselectClick(UINT uMsg, WPARAM wParam) {
    constexpr int kDragSlack = 4; // px

    switch (uMsg) {
    case WM_MOUSEMOVE: {
        if (!deselect_armed) break;
        if (!isCursorVisible()) { // the game took the mouse — this is a camera drag
            deselect_armed = false;
            break;
        }
        POINT at{};
        if (!GetCursorPos(&at)) break;
        if (std::abs(at.x - deselect_origin.x) > kDragSlack || std::abs(at.y - deselect_origin.y) > kDragSlack) {
            deselect_armed = false;
        }
        break;
    }
    case WM_LBUTTONUP:
        if (deselect_armed && isCursorVisible()) pending_deselect.store(true);
        deselect_armed = false;
        break;
    default: break;
    }
    return 1;
}

UINT onWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg != WM_LBUTTONDOWN) return trackDeselectClick(uMsg, wParam);

    deselect_armed = false;
    if (!state.loaded || !state.settings.select_in_viewport) return 1;
    if (ImGui::GetIO().WantCaptureMouse) return 1; // an addon window owns this click
    if (!isCursorVisible()) return 1;

    // A trail point handle wins over the marker behind it: the handles are small, only the
    // open trail has them, and they are drawn precisely to be aimed at.
    const size_t point = world::renderer::hoveredTrailPoint();
    const size_t hovered = world::renderer::hoveredPathable();
    if (point == pack::kNoIndex && hovered == pack::kNoIndex) {
        // Nothing under the cursor. A plain click here deselects, so watch for the button
        // coming back up without a drag in between. Not with ctrl or shift held: those mean
        // "add to what I have", and adding nothing should not throw the rest away.
        if ((wParam & (MK_CONTROL | MK_SHIFT)) == 0 && GetCursorPos(&deselect_origin)) deselect_armed = true;
        return 1;
    }

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

    // Null for a pack made in the editor: there is nothing to carry forward, so the archive
    // is built from the pack's own files alone.
    const pack::SaveResult result = pack::buildZip(state.pack, resources::source(), target);
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
        // The pack now lives in a zip, whatever it was read from. Without this a folder
        // pack saved with Save As keeps from_directory set, and the next plain Save tries
        // to write its .xml files *into* the archive's path as if it were a folder.
        state.pack.from_directory = false;
        state.pack.source_path = target;
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

// A pack that exists only in memory: no source, no path, and dirty from the start, so
// closing it warns and saving it has to go through Save As.
void newPack(const std::string& file_name, const std::string& category_name, const std::string& display_name) {
    state.reset();
    trail_edit::close();
    undo::clear();
    resources::setSource(nullptr);
    open_pack_path.clear();

    state.pack = pack::createPack(file_name, category_name, display_name);
    if (state.pack.categories.empty()) {
        // The only way this fails is a name that cannot be an id segment.
        state.reset();
        status_message = "That name will not work: it is empty or has a dot in it.";
        return;
    }

    state.rebuildIndex();
    state.loaded = true;
    // Building it is not an edit anyone should be able to undo their way out of.
    undo::clear();
    state.dirty = true;

    if (!state.pack.categories.empty()) {
        const editor::NodeRef root{editor::NodeKind::Category, 0};
        state.selection.set(root);
        state.requestReveal(root);
    }

    status_message = "New pack. Save As writes it to a zip.";
    log(ELogLevel_INFO, status_message);
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
        if (ImGui::MenuItem("New Pack...")) requestAction(PendingAction::NewPack, owner);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Starts an empty pack. It lives in memory until you Save As.");

        if (ImGui::MenuItem("Open Pack...", nullptr, false, !file_dialog::isPending())) {
            dialog_purpose = DialogPurpose::OpenFile;
            file_dialog::requestOpenFile(blishhudMarkersFolder(), {{"Marker pack", "zip,taco"}});
        }
        if (ImGui::MenuItem("Open Folder...", nullptr, false, !file_dialog::isPending())) {
            dialog_purpose = DialogPurpose::OpenFolder;
            file_dialog::requestOpenFolder(last_folder.empty() ? blishhudMarkersFolder() : last_folder);
        }

        ImGui::Separator();

        // A pack that has never been saved has nothing to reload from.
        if (ImGui::MenuItem("Reload", nullptr, false, state.loaded && !open_pack_path.empty())) {
            requestAction(PendingAction::Reload, owner);
        }
        if (ImGui::MenuItem("Close", nullptr, false, state.loaded)) requestAction(PendingAction::Close, owner);

        if (ImGui::MenuItem("Save", nullptr, false, state.loaded && state.dirty)) {
            if (open_pack_path.empty()) {
                // Never saved: Save is Save As, or it would have nowhere to write.
                dialog_purpose = DialogPurpose::SaveAs;
                file_dialog::requestSaveFile(blishhudMarkersFolder(), "markers.zip", {{"Marker pack", "zip,taco"}});
            } else {
                savePack();
            }
        }
        if (ImGui::IsItemHovered() && state.loaded) {
            ImGui::SetTooltip(
                open_pack_path.empty()      ? "Asks where to put it: this pack has never been saved."
                : state.pack.from_directory ? "Rewrites the changed .xml files in place."
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
    // Needs answers before it can do anything, and a popup belongs to the window whose
    // menu asked — the same one the discard prompt would have used.
    case PendingAction::NewPack: open_new_pack_popup = true; break;
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

// The details a pack cannot be started without: what its one category is called, and what
// file to put it in. Everything else is added afterwards like any other edit.
void drawNewPackPopup(ui::windows::Id owner) {
    if (owner != confirm_owner) return;

    constexpr const char* kTitle = "New pack";

    if (open_new_pack_popup) {
        ImGui::OpenPopup(kTitle);
        open_new_pack_popup = false;
    }

    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::PushItemWidth(260.0f);
    ImGui::InputText("Name", new_pack_name, sizeof(new_pack_name));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The root category's id. Everything in the pack sits under it. No dots.");

    ImGui::InputText("Display name", new_pack_display, sizeof(new_pack_display));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What Blish HUD shows in the marker list.");

    ImGui::InputText("File", new_pack_file, sizeof(new_pack_file));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The .xml inside the zip. More files can be added later by hand.");
    ImGui::PopItemWidth();

    ImGui::TextDisabled("Lives in memory until Save As writes the zip.");
    ImGui::Separator();

    if (ImGui::Button("Create", ImVec2(110, 0))) {
        newPack(new_pack_file, new_pack_name, new_pack_display);
        if (state.loaded) ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
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
        drawNewPackPopup(id);
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

// imgui 1.80 predates BeginDisabled (see CLAUDE.md), so a disabled button is one drawn dim
// whose press is ignored. It still hovers, which is what carries the reason why.
bool button(const char* label, bool enabled) {
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
    const bool pressed = ImGui::Button(label);
    if (!enabled) ImGui::PopStyleVar();
    return pressed && enabled;
}

// Everything the create buttons need to agree on before they do anything.
bool canCreate(bool needs_character) {
    if (!state.loaded) {
        status_message = "Open a pack first.";
        return false;
    }
    if (needs_character && !frame_camera.valid) {
        status_message = "No character position: MumbleLink has no data yet.";
        return false;
    }
    return true;
}

// Adds a marker where the character is standing. Placement follows the selection, so the
// button needs no target of its own — see pathable_edit::addPoi.
void drawAddPoi() {
    if (!button("Add POI", state.loaded)) {
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                state.loaded ? "Adds a marker at the character's position.\n"
                               "With a category selected it goes in at the end;\n"
                               "with a marker selected it follows it and copies its attributes."
                             : "Open or create a pack first.");
        }
        return;
    }

    if (!canCreate(true)) return;

    const size_t added = pathable_edit::addPoi(state, frame_camera.player, static_cast<int32_t>(frame_camera.map_id));
    if (added == pack::kNoIndex) {
        status_message = "Select a category or a marker to say where the POI goes.";
    } else {
        status_message = "Added a POI at the character's position.";
    }
}

void drawAddTrail() {
    if (!button("Add Trail", state.loaded)) {
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                state.loaded ? "Adds an empty trail and a .trl for it to point at.\n"
                               "Add its points in the Trail Editor."
                             : "Open or create a pack first.");
        }
        return;
    }

    if (!canCreate(true)) return;

    const size_t added = pathable_edit::addTrail(state, static_cast<int32_t>(frame_camera.map_id));
    if (added == pack::kNoIndex) {
        status_message = "Select a category or a marker to say where the trail goes.";
        return;
    }

    // Nothing to look at until it has points, so send the user where they are added.
    ui::windows::visible(ui::windows::Id::TrailEditor) = true;
    status_message = "Added an empty trail. Add points in the Trail Editor.";
}

// A category needs a name, so the button opens a prompt rather than acting on the spot.
bool open_category_popup = false;
char new_category_name[64] = "";

void drawAddCategory() {
    if (button("Add Category...", state.loaded)) {
        if (canCreate(false)) {
            new_category_name[0] = '\0';
            open_category_popup = true;
        }
    } else if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            state.loaded ? "Adds a category under the selected one,\n"
                           "or at the top level with nothing selected."
                         : "Open or create a pack first.");
    }

    constexpr const char* kTitle = "New category";
    if (open_category_popup) {
        ImGui::OpenPopup(kTitle);
        open_category_popup = false;
    }

    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const editor::NodeRef selected = state.selection.primary();
    if (selected.kind == editor::NodeKind::Category && selected.index < state.pack.categories.size()) {
        ImGui::TextDisabled("Under %s", state.pack.categories[selected.index].id.c_str());
    } else {
        ImGui::TextDisabled("At the top level");
    }

    ImGui::SetNextItemWidth(260.0f);
    const bool submitted = ImGui::InputText("Name", new_category_name, sizeof(new_category_name), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextDisabled("Becomes the last part of the category's id. No dots.");
    ImGui::Separator();

    const bool create = ImGui::Button("Create", ImVec2(110, 0)) || submitted;
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();

    if (create) {
        const size_t added = pathable_edit::addCategory(state, new_category_name);
        if (added == pack::kNoIndex) {
            status_message = "Could not add that category: the name is empty, has a dot in it, or is already taken.";
        } else {
            status_message = "Added category " + state.pack.categories[added].id;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

// No menu bar: this one is a strip of controls, and a menu bar over it would be taller
// than its contents.
void drawToolbarWindow() {
    if (!beginWindow(ui::windows::Id::Toolbar, ImVec2(460, 120), false)) return;

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
    ImGui::SameLine();
    drawAddTrail();
    ImGui::SameLine();
    drawAddCategory();

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

// A viewport click that hit nothing. Clearing the selection is the point — but not while a
// trail is open in the editor and what is selected is part of it.
//
// Editing a trail means aiming at its point handles one at a time, and they are 4px dots. A
// miss that threw the trail away too would close the editor and cost a trip back to the tree
// before the next point could be touched, which makes the whole loop hostile. So a miss falls
// back to the trail itself: the points let go, the trail stays selected, the editor stays
// open, and the next click can go straight at another handle.
//
// A miss never *creates* a selection, so an empty one is left alone rather than being given
// the open trail.
void deselectInViewport(editor::State& state) {
    if (trail_edit::isOpen() && !state.selection.empty()) {
        const size_t trail = trail_edit::pathable();

        bool all_within = true;
        for (editor::NodeRef node : state.selection.items()) {
            const bool its_point = node.kind == editor::NodeKind::TrailPoint && node.index == trail;
            const bool the_trail = node.kind == editor::NodeKind::Pathable && node.index == trail;
            if (!its_point && !the_trail) {
                all_within = false;
                break;
            }
        }

        // Something else is selected as well, so the trail is not what is being worked on and
        // has no claim on the click.
        if (all_within) {
            state.selection.set(editor::NodeRef{editor::NodeKind::Pathable, trail});
            return;
        }
    }

    state.selection.clear();
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
    // Compares and writes only when something moved, so this is free on almost every frame.
    settings::update(state.settings, last_folder, last_image_folder);

    // The trail editor's window is what makes a trail "open", and a closed window draws
    // nothing — including its followSelection — so the close has to happen out here. The
    // working copy is committed on every edit, so nothing is lost by dropping it.
    if (!ui::windows::visible(ui::windows::Id::TrailEditor)) trail_edit::close();

    // The world is drawn whether or not any editor window is open — the point is to see
    // the pack in the game.
    // ImGui's display size, not NexusLinkData's — see fromMumble. Everything below draws
    // into an ImGui draw list, so that is the space the projection has to land in.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const world::Camera camera = world::fromMumble(mumble_link, glm::vec2(display.x, display.y));
    frame_camera = camera;
    world::renderer::draw(state, camera);

    // After the markers so it draws over them, and so it can claim the click first.
    const bool gizmo_active = world::gizmo::draw(state, camera);

    const size_t picked = pending_select.exchange(pack::kNoIndex);
    const size_t picked_point = pending_select_point.exchange(pack::kNoIndex);
    const bool deselect = pending_deselect.exchange(false);

    // Same gizmo guard as a pick: ImGuizmo::IsOver() is only knowable here, and a click on
    // the gizmo is aimed at what is already selected, not at the empty world behind it.
    if (deselect && !gizmo_active) deselectInViewport(state);

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

void load(AddonAPI* _api, Mumble::Data* _mumble_link) {
    api = _api;
    mumble_link = _mumble_link;

    resources::init(api);
    // Images added in the editor are not in the DataSource until a save, so the cache has
    // to be able to see the pack's pending adds. `state` outlives every load.
    resources::setPendingAssets(&state.pack);

    settings::init(api);
    settings::load(&state.settings, &last_folder, &last_image_folder);

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
