#include "editor/marker_editor.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "editor/editor_state.h"
#include "editor/file_dialog.h"
#include "editor/pathable_edit.h"
#include "editor/settings.h"
#include "editor/trail_edit.h"
#include "editor/ui/property_panel.h"
#include "editor/ui/resource_window.h"
#include "editor/ui/trail_editor.h"
#include "editor/ui/tree_window.h"
#include "editor/undo.h"
#include "editor/world/gizmo.h"
#include "editor/world/overlay.h"
#include "pack/source.h"
#include "pack/writer.h"
#include "pack/xml_reader.h"
#include "util/path_text.h"
#include "viewer/desktop.h"
#include "viewer/game/camera.h"
#include "viewer/game/rtapi.h"
#include "viewer/live/edited.h"
#include "viewer/packs/registry.h"
#include "viewer/render/scene.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/glyphs.h"
#include "viewer/ui/style.h"
#include "viewer/viewer.h"

namespace marker_editor {

namespace {

// ── Across threads ────────────────────────────────────────────────────────────
// Nexus runs onWndProc on the game's message thread and a bind's handler on a thread of its
// own, and neither may touch the Editor: they leave what they saw here for the frame, and read
// only what the frame published here. Static, so they outlive every Editor.

// Whether the editor runs: from Open Editor until it has no pack open and no window on screen.
std::atomic<bool> active{false};

// Published by each frame for onWndProc: whether a world click may pick, which needs a pack
// open and Select in Viewport on, and whether an ImGui window is under the cursor, which
// ImGui knows only on the render thread.
std::atomic<bool> picking{false};
std::atomic<bool> mouse_over_ui{false};

std::atomic<bool> undo_requested{false};
std::atomic<bool> redo_requested{false};

// A world click picked this pathable; the render thread consumes it. See onWndProc for why the
// click cannot come from ImGui.
std::atomic<size_t> pending_select{pack::kNoIndex};
// The same for a point of the trail open in the editor. Which trail it belongs to is not
// carried across: the render thread owns that and reads it when it consumes this.
std::atomic<size_t> pending_select_point{pack::kNoIndex};
// Latched with the click rather than read on the render thread, where the key may already
// have come back up.
std::atomic<bool> pending_select_additive{false};

// A world click that hit nothing wants to clear the selection — but only if it was a click
// and not the start of a camera drag, which cannot be known until the button comes back up.
// See onWndProc.
std::atomic<bool> pending_deselect{false};
bool deselect_armed = false; // message thread only
POINT deselect_origin{}; // message thread only

// Ctrl and shift both mean "add to what I have" out here. The viewport has no rows to run
// between, so a range is the one thing shift cannot mean — which is why this does not go
// through editor::clickMods.
editor::ClickMods viewportMods() {
    return pending_select_additive.load() ? editor::ClickMods::Toggle : editor::ClickMods::Replace;
}

// A click on nothing clears the selection, but a left drag on nothing turns the camera, so it is
// decided when the button comes back up. Down and up positions cannot tell them apart: Nexus's
// `MouseResetFix` puts the cursor back where it went down. The cursor going hidden (mouselook)
// can, with movement past a few pixels for a drag that keeps it visible.
//
// Returns 1 throughout: the game has to keep seeing these, or the camera stops working.
UINT trackDeselectClick(UINT message) {
    constexpr int kDragSlack = 4; // px

    switch (message) {
    case WM_MOUSEMOVE: {
        if (!deselect_armed) break;
        if (!viewer::desktop::cursorVisible()) { // the game took the mouse — this is a camera drag
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
        if (deselect_armed && viewer::desktop::cursorVisible()) pending_deselect.store(true);
        deselect_armed = false;
        break;
    default: break;
    }
    return 1;
}

// A world click never reaches ImGui: Nexus forwards WM_LBUTTONDOWN to it only when
// io.WantCaptureMouse is already true. So picking happens here, on the message thread. Returning 0
// consumes the message (Nexus's RiApi.cpp), done only when something was hit.
UINT onWndProc(HWND, UINT message, WPARAM key_state, LPARAM) {
    if (!active.load()) return 1;
    if (message != WM_LBUTTONDOWN) return trackDeselectClick(message);

    deselect_armed = false;
    if (!picking.load()) return 1;
    if (mouse_over_ui.load()) return 1; // an addon window owns this click
    if (!viewer::desktop::cursorVisible()) return 1;

    // A trail point handle wins over the marker behind it: the handles are small, only the
    // open trail has them, and they are drawn precisely to be aimed at.
    const size_t point = world::overlay::hoveredTrailPoint();
    const size_t hovered = world::overlay::hoveredPathable();
    if (point == pack::kNoIndex && hovered == pack::kNoIndex) {
        // Nothing under the cursor. A plain click here deselects, so watch for the button
        // coming back up without a drag in between. Not with ctrl or shift held: those mean
        // "add to what I have", and adding nothing should not throw the rest away.
        if ((key_state & (MK_CONTROL | MK_SHIFT)) == 0 && GetCursorPos(&deselect_origin)) deselect_armed = true;
        return 1;
    }

    // Shift does what ctrl does out here. A range needs an order between two markers, and
    // the world has none — the tree is where a range means something.
    pending_select_additive.store((key_state & (MK_CONTROL | MK_SHIFT)) != 0);
    if (point != pack::kNoIndex) {
        pending_select_point.store(point);
    } else {
        pending_select.store(hovered);
    }
    return 0;
}

void onEditKeybind(const char* identifier, bool is_release) {
    if (is_release) return;
    if (std::strcmp(identifier, KEYBIND_UNDO_ID) == 0) {
        undo_requested = true;
    } else if (std::strcmp(identifier, KEYBIND_REDO_ID) == 0) {
        redo_requested = true;
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// A menu item's label with its icon; its text alone where the icons do not draw.
std::string icon(const char* glyph, const char* text) {
    return viewer::ui::fonts::label(glyph, text);
}

// <Documents>/Guild Wars 2/addons/blishhud/markers, where Pathing looks for packs, or its deepest
// ancestor that exists: a dialog handed a missing folder opens wherever Windows was last.
std::filesystem::path blishhudMarkersFolder() {
    std::filesystem::path path = viewer::registry::blishMarkersFolder();

    std::error_code ec;
    while (!path.empty() && !std::filesystem::is_directory(path, ec)) {
        const std::filesystem::path parent = path.parent_path();
        if (parent == path) return {};
        path = parent;
    }
    return path;
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

// A viewport click that hit nothing. Only the points let go: the handles are small, and a miss
// that dropped the trail too would close the trail editor before the next aim.
void deselectInViewport(editor::State& state) {
    if (!state.points.empty()) {
        state.points.indices.clear();
        return;
    }

    // Nor the trail alone: it is deselected from the tree, or by picking something else.
    const editor::NodeRef open_trail{editor::NodeKind::Pathable, trail_edit::pathable(state)};
    if (trail_edit::isOpen(state) && state.selection.size() == 1 && state.selection.primary() == open_trail) return;

    state.selection.clear();
}

// What the in-flight file dialog is for. One dialog runs at a time, so a single value is
// enough, and it has to survive until the worker reports back.
enum class DialogPurpose { OpenFile, OpenFolder, SaveAs, AddResource, ExportTrail };

// Close and Reload with unsaved edits ask first. The confirmation belongs to the window whose
// menu asked: an ImGui popup is scoped to its window, and every large window draws the menu bar.
enum class PendingAction { None, Close, Reload, NewPack, CloseEditor };

// ── The editor ────────────────────────────────────────────────────────────────

// Everything the editor holds, from load to unload. It runs from start() until it has no pack
// open and no window on screen; stopping drops the pack and lets go of the binds.
class Editor
{
public:
    Editor(AddonAPI* api, Mumble::Data* mumble_link)
        : api(api)
        , mumble_link(mumble_link)
        , settings_file(api) {
        state.resources.setApi(api);
    }

    // Saved or not, what is open goes with the addon.
    ~Editor() { stop(); }

    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    void start();
    void render();
    viewer::runtime::LiveMap* editedLive(int map_id, viewer::TextureLookup* texture);

    ui::windows::Shown windows;

private:
    void stop();
    void publish();
    void log(ELogLevel level, const std::string& message);
    void bindHistory(bool bound);

    std::filesystem::path packFolder() const;
    void rememberPackFolder(const std::filesystem::path& path);
    std::filesystem::path imageFolder() const;

    void openPack(std::filesystem::path path);
    void savePackTo(const std::filesystem::path& target, bool as_directory);
    void savePack();
    void newPack(const std::string& file_name, const std::string& category_name, const std::string& display_name);
    void closePack();
    void takeDialogResult();

    void applyUndo();
    void applyRedo();
    void applyRequestedHistory();
    void applyPicks(bool gizmo_active);

    void requestAction(PendingAction action, ui::windows::Id owner);
    void applyPendingAction();

    void drawPack(const viewer::Camera& camera);
    void drawMenuBar(ui::windows::Id owner);
    void drawConfirmPopup(ui::windows::Id owner);
    void drawNewPackPopup(ui::windows::Id owner);
    bool beginWindow(ui::windows::Id id, const ImVec2& first_size, bool with_menus);
    void drawPackHeader();
    void drawTreeWindow(const ui::tree_window::Reveal& reveal);
    void drawDetailsWindow();
    void drawToolbarWindow();
    void drawTrailEditorWindow();
    void drawResourcesWindow();
    bool canCreate(bool needs_character);
    void drawAddPoi();
    void drawAddTrail();
    size_t categoryPopupReference() const;
    void drawAddCategory();

    AddonAPI* api;
    Mumble::Data* mumble_link;

    file_dialog::FileDialog dialog;
    DialogPurpose dialog_purpose = DialogPurpose::OpenFile;

    settings::File settings_file;
    // Read on the first start, not at load: a player who never edits never has them read.
    bool settings_loaded = false;

    editor::State state;
    // Absolute path of the pack currently open, empty when none is.
    std::filesystem::path open_pack_path;
    std::string status;

    // Where the folder browser starts next time, and likewise for adding images.
    std::filesystem::path last_folder;
    std::filesystem::path last_image_folder;

    ui::property_panel::View property_view;
    ui::trail_editor::View trail_view;
    ui::resource_window::Window resource_window;

    // The open pack as a live map, which the viewer draws beside whatever it draws of the same file.
    viewer::edited::EditedLive edited_live;

    // This frame's camera, kept so the menu bar can reach the player position and map id
    // without every window that draws the bar having to be handed a camera.
    viewer::Camera frame_camera;

    PendingAction pending_action = PendingAction::None;
    bool confirm_pending = false; // the action is waiting on the modal
    bool open_confirm_popup = false; // OpenPopup on the owner's next frame
    ui::windows::Id confirm_owner = ui::windows::Id::Tree;

    // New Pack asks for a few details first, in a popup owned the same way.
    bool open_new_pack_popup = false;
    std::string new_pack_name = "mypack";
    std::string new_pack_display = "My Pack";
    std::string new_pack_file = "markers.xml";

    // A category needs a name, so the button opens a prompt rather than acting on the spot.
    bool open_category_popup = false;
    std::string new_category_name;
    std::string new_category_display_name;
    std::string new_category_description;
    bool new_category_copy_attributes = true;
};

void Editor::log(ELogLevel level, const std::string& message) {
    api->Log(level, viewer::ADDON_NAME, message.c_str());
}

// Where a pack browser starts: wherever packs were last opened or saved, else the Blish
// HUD markers folder.
std::filesystem::path Editor::packFolder() const {
    std::error_code ec;
    if (!last_folder.empty() && std::filesystem::is_directory(last_folder, ec)) return last_folder;
    return blishhudMarkersFolder();
}

// A zip contributes the folder holding it; a folder pack is one.
void Editor::rememberPackFolder(const std::filesystem::path& path) {
    if (path.empty()) return;
    std::error_code ec;
    last_folder = std::filesystem::is_directory(path, ec) ? path : path.parent_path();
}

std::filesystem::path Editor::imageFolder() const {
    std::error_code ec;
    if (!last_image_folder.empty() && std::filesystem::is_directory(last_image_folder, ec)) return last_image_folder;
    return packFolder();
}

// By value on purpose: Reload passes open_pack_path, which this function reassigns
// partway through. A reference would alias it and change under us.
void Editor::openPack(std::filesystem::path path) {
    // On the render thread: 56k markers has not stuttered (docs/editor_plan.md). The undo
    // records and the open trail belong to the pack going away, and go with it.
    state.reset();

    // Zip or folder — pack::open picks by what the path actually is.
    std::string error;
    std::unique_ptr<pack::DataSource> source = pack::open(path, &error);
    if (!source) {
        status = "Could not open " + util::pathText(path.filename()) + ": " + error;
        log(ELogLevel_WARNING, status);
        return;
    }

    state.pack = pack::loadPack(*source);
    viewer::ui::fonts::require(viewer::ui::glyphs::of(state.pack));

    // Icons and .trl data are read straight out of the source as markers become visible, so
    // it has to outlive the load.
    state.resources.setSource(std::move(source));

    state.rebuildIndex();
    state.loaded = true;
    open_pack_path = state.pack.source_path;
    rememberPackFolder(state.pack.source_path);

    status = std::to_string(state.pack.categories.size()) + " categories, " +
             std::to_string(state.pack.countByType(pack::PathableType::Poi)) + " POIs, " +
             std::to_string(state.pack.countByType(pack::PathableType::Trail)) + " trails";
    if (!state.pack.warnings.empty()) {
        status += ", " + std::to_string(state.pack.warnings.size()) + " warnings";
        for (const pack::Warning& warning : state.pack.warnings) {
            log(ELogLevel_WARNING, warning.file + ": " + warning.message);
        }
    }
    log(ELogLevel_INFO, "Opened " + util::pathText(path.filename()) + " — " + status);
}

// EditorHooks::edited_live: the open pack as a live map on `map_id`, with the tree's eye
// toggles, and the editor's reads of its files, which see what is not saved yet. The viewer
// draws it with the installed packs, so it looks as it will once installed.
viewer::runtime::LiveMap* Editor::editedLive(int map_id, viewer::TextureLookup* texture) {
    viewer::edited::Report report;
    if (active.load() && state.loaded) {
        report.path = open_pack_path;
        report.pack = &state.pack;
        report.revision = state.revision;
        report.shown = &state.pathable_visible;
    }
    resources::Cache& cache = state.resources;
    edited_live.update(
        report,
        map_id,
        [&cache](const std::string& path, bool* deferred) { return cache.trail(path, deferred); },
        [&cache](const std::string& path, bool* deferred) { return cache.trailColour(path, deferred); });
    if (!report.pack) return nullptr;
    *texture = [&cache](size_t, const std::string& path) { return cache.texture(path); };
    return &edited_live.live;
}

// What only an editor draws over its pack, which the viewer drew: outlines, handles, picking,
// placed by the items of the viewer's scene that carry a pathable — the open pack's.
void Editor::drawPack(const viewer::Camera& camera) {
    if (!state.loaded || !camera.valid || camera.map_open) return;
    if (const std::shared_ptr<const viewer::Scene> scene = viewer::published()) world::overlay::draw(state, camera, *scene);
}

// Writes the pack back to `target`. A folder pack updates its changed .xml files in place;
// a zip is rebuilt whole, since every other entry has to come along with it.
void Editor::savePackTo(const std::filesystem::path& target, bool as_directory) {
    if (!state.loaded) return;

    if (as_directory) {
        const pack::SaveResult result = pack::saveToDirectory(state.pack, target);
        if (!result.ok) {
            status = "Save failed: " + result.error;
            log(ELogLevel_WARNING, status);
            return;
        }
        for (const std::string& warning : result.warnings) log(ELogLevel_WARNING, warning);

        // They are on disk now, so they are no longer pending.
        state.pack.clearAssetChanges();
        state.dirty = false;
        status =
            "Saved " + std::to_string(result.entries_written) + " changed file(s)" + assetSummary(result) + " to " + util::pathText(target);
        log(ELogLevel_INFO, status);
        return;
    }

    // Null for a pack made in the editor: there is nothing to carry forward, so the archive
    // is built from the pack's own files alone.
    const pack::SaveResult result = pack::buildZip(state.pack, state.resources.source(), target);
    if (!result.ok) {
        status = "Save failed: " + result.error;
        log(ELogLevel_WARNING, status);
        pack::abortZip(target);
        return;
    }
    for (const std::string& warning : result.warnings) log(ELogLevel_WARNING, warning);

    // The archive we are about to replace is the one we still have open for reading, and
    // Windows will not rename over a live handle. Drop it, swap, then reopen.
    state.resources.setSource(nullptr);

    std::string error;
    if (!pack::commitZip(target, &error)) {
        status = "Save failed: " + error;
        log(ELogLevel_WARNING, status);
        pack::abortZip(target);
    } else {
        // The new archive holds them now; keeping them pending would write them again.
        state.pack.clearAssetChanges();
        state.dirty = false;
        open_pack_path = target;
        rememberPackFolder(target);
        // The pack now lives in a zip, whatever it was read from. Without this a folder
        // pack saved with Save As keeps from_directory set, and the next plain Save tries
        // to write its .xml files *into* the archive's path as if it were a folder.
        state.pack.from_directory = false;
        state.pack.source_path = target;
        status = "Saved " + std::to_string(result.entries_written) + " entries (" + std::to_string(result.files_rewritten) +
                 " xml rewritten)" + assetSummary(result) + " to " + util::pathText(target.filename());
        log(ELogLevel_INFO, status);
    }

    // Reopen either way: without a source the icons and trails stop resolving.
    std::unique_ptr<pack::DataSource> reopened = pack::open(open_pack_path, &error);
    if (reopened) {
        state.resources.setSource(std::move(reopened));
    } else {
        log(ELogLevel_WARNING, "Could not reopen the pack after saving: " + error);
    }
}

void Editor::savePack() {
    if (!state.loaded) return;
    savePackTo(open_pack_path, state.pack.from_directory);
}

// A pack that exists only in memory: no source, no path, and dirty from the start, so
// closing it warns and saving it has to go through Save As.
void Editor::newPack(const std::string& file_name, const std::string& category_name, const std::string& display_name) {
    state.reset();
    state.resources.setSource(nullptr);
    open_pack_path.clear();

    state.pack = pack::createPack(file_name, category_name, display_name);
    if (state.pack.categories.empty()) {
        // The only way this fails is a name that cannot be an id segment.
        state.reset();
        status = "That name will not work: it is empty or has a dot in it.";
        return;
    }

    state.rebuildIndex();
    state.loaded = true;
    // Building it is not an edit anyone should be able to undo their way out of.
    undo::clear(state);
    state.dirty = true;

    const editor::NodeRef root{editor::NodeKind::Category, 0};
    state.selection.set(root);
    state.requestReveal(root);

    status = "New pack. Save As writes it to a zip.";
    log(ELogLevel_INFO, status);
}

void Editor::closePack() {
    state.reset();
    state.resources.setSource(nullptr);
    open_pack_path.clear();
    status.clear();
}

// Undo can change the bytes behind the open trail without going through the trail editor,
// so the working copy has to be told to reread.
void Editor::applyUndo() {
    undo::undo(state);
    trail_edit::refresh(state);
}

void Editor::applyRedo() {
    undo::redo(state);
    trail_edit::refresh(state);
}

// Nexus binds are global, so they fire while a text field has the caret too — where Ctrl+Z
// means "undo my typing" to ImGui and would mean "undo an edit" here. The field wins.
void Editor::applyRequestedHistory() {
    const bool undo = undo_requested.exchange(false);
    const bool redo = redo_requested.exchange(false);
    if (!state.loaded || ImGui::GetIO().WantTextInput) return;
    if (undo) applyUndo();
    if (redo) applyRedo();
}

void Editor::requestAction(PendingAction action, ui::windows::Id owner) {
    pending_action = action;
    confirm_owner = owner;
    // Nothing to lose with a clean pack: it goes ahead at the end of the frame.
    confirm_pending = state.dirty;
    open_confirm_popup = state.dirty;
}

// The same bar in every large window, so File and View are reachable from whichever one
// the user kept open. `owner` is the window drawing it — see the confirmation popup.
void Editor::drawMenuBar(ui::windows::Id owner) {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem(icon(ICON_FA_FILE, "New Pack...").c_str())) requestAction(PendingAction::NewPack, owner);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Starts an empty pack. It lives in memory until you Save As.");

        if (ImGui::MenuItem(icon(ICON_FA_FOLDER_OPEN, "Open Pack...").c_str(), nullptr, false, !dialog.isPending())) {
            dialog_purpose = DialogPurpose::OpenFile;
            dialog.requestOpenFile(packFolder(), {{"Marker pack", "zip,taco"}});
        }
        if (ImGui::MenuItem(icon(ICON_FA_FOLDER, "Open Folder...").c_str(), nullptr, false, !dialog.isPending())) {
            dialog_purpose = DialogPurpose::OpenFolder;
            dialog.requestOpenFolder(packFolder());
        }

        ImGui::Separator();

        // A pack that has never been saved has nothing to reload from.
        if (ImGui::MenuItem(icon(ICON_FA_ROTATE_RIGHT, "Reload").c_str(), nullptr, false, state.loaded && !open_pack_path.empty())) {
            requestAction(PendingAction::Reload, owner);
        }
        if (ImGui::MenuItem(icon(ICON_FA_XMARK, "Close").c_str(), nullptr, false, state.loaded)) requestAction(PendingAction::Close, owner);

        if (ImGui::MenuItem(icon(ICON_FA_FLOPPY_DISK, "Save").c_str(), nullptr, false, state.loaded && state.dirty)) {
            if (open_pack_path.empty()) {
                // Never saved: Save is Save As, or it would have nowhere to write.
                dialog_purpose = DialogPurpose::SaveAs;
                dialog.requestSaveFile(packFolder(), "markers.zip", {{"Marker pack", "zip,taco"}});
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

        if (ImGui::MenuItem(icon(ICON_FA_FLOPPY_DISK, "Save As...").c_str(), nullptr, false, state.loaded && !dialog.isPending())) {
            dialog_purpose = DialogPurpose::SaveAs;
            const std::string suggested =
                open_pack_path.empty() ? std::string("markers.zip") : util::pathText(open_pack_path.filename().replace_extension(".zip"));
            dialog.requestSaveFile(packFolder(), suggested, {{"Marker pack", "zip,taco"}});
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Always saves as a .zip.");

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        const std::string undo_label = undo::canUndo(state) ? "Undo " + undo::undoLabel(state) : std::string("Undo");
        const std::string redo_label = undo::canRedo(state) ? "Redo " + undo::redoLabel(state) : std::string("Redo");

        if (ImGui::MenuItem(icon(ICON_FA_ROTATE_LEFT, undo_label.c_str()).c_str(), "Ctrl+Z", false, undo::canUndo(state))) applyUndo();
        if (ImGui::MenuItem(icon(ICON_FA_ROTATE_RIGHT, redo_label.c_str()).c_str(), "Ctrl+Y", false, undo::canRedo(state))) applyRedo();

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ui::windows::drawViewMenu(windows);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Options")) {
        ImGui::MenuItem("3D Highlight", nullptr, &state.settings.highlight_3d);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Outline the selection in the world.");

        ImGui::MenuItem("Select in Viewport", nullptr, &state.settings.select_in_viewport);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click a marker in the world to select it.");

        ImGui::EndMenu();
    }

    // Last, and only where it belongs: the Trail menu acts on the trail editor's own
    // state, so it has no business hanging off the tree or the details window.
    if (owner == ui::windows::Id::TrailEditor) {
        if (ui::trail_editor::drawMenu(state, trail_view, frame_camera) == ui::trail_editor::Request::Export && !dialog.isPending()) {
            dialog_purpose = DialogPurpose::ExportTrail;
            dialog.requestSaveFile(packFolder(), "trail.trl", {{"Trail data", "trl"}});
        }
    }

    ImGui::EndMenuBar();
}

// Deferred to the end of the frame: closing or reloading frees the vectors every window
// has just drawn from, and a menu click happens partway through that.
void Editor::applyPendingAction() {
    if (confirm_pending) {
        // The window carrying the modal was closed out from under it, taking the popup
        // with it. Drop the action rather than leaving it armed for the next frame.
        if (!windows[confirm_owner]) {
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
    // The session ends with its main window: the pack goes, and every window with it.
    case PendingAction::CloseEditor:
        closePack();
        windows[ui::windows::Id::Tree] = false;
        break;
    case PendingAction::None: break;
    }
    pending_action = PendingAction::None;
}

// Runs after the menu bar, in the window that owns the request: a popup cannot be opened
// from inside a menu, and it has to be begun from the same window every frame.
void Editor::drawConfirmPopup(ui::windows::Id owner) {
    if (!confirm_pending || owner != confirm_owner) return;

    constexpr const char* kTitle = "Discard Changes?";

    if (open_confirm_popup) {
        ImGui::OpenPopup(kTitle);
        open_confirm_popup = false;
    }

    if (ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This pack has unsaved changes.");
        ImGui::TextDisabled("%s", pending_action == PendingAction::Reload ? "Reloading will discard them." : "Closing will discard them.");
        ImGui::Separator();

        const std::string discard = icon(ICON_FA_TRASH, "Discard");
        const std::string save_first = icon(ICON_FA_FLOPPY_DISK, "Save First");
        const float width = viewer::ui::style::buttonWidth({discard.c_str(), save_first.c_str(), "Cancel"});
        if (ImGui::Button(discard.c_str(), ImVec2(width, 0))) {
            confirm_pending = false; // applied at the end of the frame
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(save_first.c_str(), ImVec2(width, 0))) {
            savePack();
            if (state.dirty) pending_action = PendingAction::None; // the save failed, so keep the pack
            confirm_pending = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(width, 0))) {
            pending_action = PendingAction::None;
            confirm_pending = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// What a new pack needs: its one category's name and the file to put it in.
void Editor::drawNewPackPopup(ui::windows::Id owner) {
    if (owner != confirm_owner) return;

    constexpr const char* kTitle = "New Pack";

    if (open_new_pack_popup) {
        ImGui::OpenPopup(kTitle);
        open_new_pack_popup = false;
    }

    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::PushItemWidth(260.0f);
    ImGui::InputText("Name", &new_pack_name);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The root category's id. Everything in the pack sits under it. No dots.");

    ImGui::InputText("Display name", &new_pack_display);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The name shown in the markers menu.");

    ImGui::InputText("File", &new_pack_file);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The .xml inside the zip. More files can be added later by hand.");
    ImGui::PopItemWidth();

    ImGui::TextDisabled("Lives in memory until Save As writes the zip.");
    ImGui::Separator();

    const std::string create = icon(ICON_FA_CHECK, "Create");
    const float width = viewer::ui::style::buttonWidth({create.c_str(), "Cancel"});
    if (ImGui::Button(create.c_str(), ImVec2(width, 0))) {
        newPack(new_pack_file, new_pack_name, new_pack_display);
        if (state.loaded) ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(width, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// Begins one editor window and its menu bar. Returns false when the window is hidden or
// collapsed, in which case it has already been ended and the caller draws nothing;
// otherwise the caller draws its contents and calls ImGui::End itself.
bool Editor::beginWindow(ui::windows::Id id, const ImVec2& first_size, bool with_menus) {
    if (!windows[id]) return false;

    // The main window's close button ends the session, asking first when there is unsaved work,
    // so it stays open until that is answered. The others only hide.
    const bool main = id == ui::windows::Id::Tree;
    bool open = true;
    ImGui::SetNextWindowSize(first_size, ImGuiCond_FirstUseEver);
    const bool shown = ImGui::Begin(ui::windows::title(id), main ? &open : &windows[id], with_menus ? ImGuiWindowFlags_MenuBar : 0);
    if (!open) requestAction(PendingAction::CloseEditor, id);
    if (!shown) {
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

void Editor::drawPackHeader() {
    if (dialog.isPending()) {
        ImGui::TextDisabled("(file dialog open)");
    } else if (state.loaded) {
        const std::string name =
            state.pack.from_directory ? util::pathText(open_pack_path.filename()) + "/" : util::pathText(open_pack_path.filename());
        ImGui::TextDisabled("%s%s", name.c_str(), state.dirty ? " *" : "");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", util::pathText(open_pack_path).c_str());
    } else {
        ImGui::TextDisabled("No pack open. Use File > Open Pack.");
    }

    if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
    ImGui::Separator();
}

void Editor::drawTreeWindow(const ui::tree_window::Reveal& reveal) {
    if (!beginWindow(ui::windows::Id::Tree, ImVec2(360, 560), true)) return;
    drawPackHeader();
    ui::tree_window::draw(state, reveal);
    ImGui::End();
}

void Editor::drawDetailsWindow() {
    if (!beginWindow(ui::windows::Id::Details, ImVec2(420, 560), true)) return;
    ui::property_panel::draw(state, property_view);
    ImGui::End();
}

// Everything the create buttons need to agree on before they do anything.
bool Editor::canCreate(bool needs_character) {
    if (!state.loaded) {
        status = "Open a pack first.";
        return false;
    }
    if (needs_character && !frame_camera.valid) {
        status = "The game has not reported your character's position yet.";
        return false;
    }
    return true;
}

// Adds a marker where the character is standing. Placement follows the selection, so the
// button needs no target of its own — see pathable_edit::addPoi.
void Editor::drawAddPoi() {
    if (!viewer::ui::style::button(icon(ICON_FA_LOCATION_DOT, "Add POI").c_str(), state.loaded)) {
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
    status =
        added == pack::kNoIndex ? "Select a category or a marker to say where the POI goes." : "Added a POI at the character's position.";
}

void Editor::drawAddTrail() {
    if (!viewer::ui::style::button(icon(ICON_FA_ROUTE, "Add Trail").c_str(), state.loaded)) {
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
        status = "Select a category or a marker to say where the trail goes.";
        return;
    }

    // Nothing to look at until it has points, so send the user where they are added.
    windows[ui::windows::Id::TrailEditor] = true;
    status = "Added an empty trail. Add points in the Trail Editor.";
}

// The category the popup's default name and attribute copy are relative to: the selected
// category itself, or the one a selected marker is filed under. Mirrors
// pathable_edit::addCategory's own resolution, so what the popup shows is what pressing
// Create will actually do.
size_t Editor::categoryPopupReference() const {
    const editor::NodeRef selected = state.selection.primary();
    if (selected.kind == editor::NodeKind::Category && selected.index < state.pack.categories.size()) return selected.index;
    if (selected.kind == editor::NodeKind::Pathable && selected.index < state.pack.pathables.size()) {
        const size_t category = state.pack.pathables[selected.index].category;
        if (category < state.pack.categories.size()) return category;
    }
    return pack::kNoIndex;
}

void Editor::drawAddCategory() {
    if (viewer::ui::style::button(icon(ICON_FA_FOLDER_PLUS, "Add Category...").c_str(), state.loaded)) {
        if (canCreate(false)) {
            const size_t reference = categoryPopupReference();
            new_category_name = reference < state.pack.categories.size() ? state.pack.categories[reference].id : "";
            new_category_display_name.clear();
            new_category_description.clear();
            new_category_copy_attributes = true;
            open_category_popup = true;
        }
    } else if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            state.loaded ? "Adds a category at the id you give it —\n"
                           "editing the last segment of the selected one\n"
                           "makes a sibling, appending one makes a child."
                         : "Open or create a pack first.");
    }

    constexpr const char* kTitle = "New Category";
    if (open_category_popup) {
        ImGui::OpenPopup(kTitle);
        open_category_popup = false;
    }

    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const size_t reference = categoryPopupReference();
    if (reference < state.pack.categories.size()) {
        ImGui::TextDisabled("Selected: %s", state.pack.categories[reference].id.c_str());
    } else {
        ImGui::TextDisabled("Nothing selected");
    }

    ImGui::SetNextItemWidth(260.0f);
    const bool name_submitted = ImGui::InputText("Name", &new_category_name, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextDisabled(
        "The category's full id. The last segment is edited for a sibling,\nadded to for a child, trimmed to go further up.");

    ImGui::SetNextItemWidth(260.0f);
    const bool display_submitted = ImGui::InputText("Display name", &new_category_display_name, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextDisabled("Shown in the in-game menu.");

    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("Description (optional)", &new_category_description, ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::Separator();
    ImGui::Checkbox("Copy attributes of selected", &new_category_copy_attributes);
    ImGui::Separator();

    const bool enabled = !new_category_name.empty() && !new_category_display_name.empty();
    const std::string create_label = icon(ICON_FA_CHECK, "Create");
    const float width = viewer::ui::style::buttonWidth({create_label.c_str(), "Cancel"});
    const bool create_pressed = viewer::ui::style::button(create_label.c_str(), enabled, width);
    if (!enabled && ImGui::IsItemHovered()) ImGui::SetTooltip("Give it a name and a display name first.");
    const bool create = create_pressed || (enabled && (name_submitted || display_submitted));
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(width, 0))) ImGui::CloseCurrentPopup();

    if (create) {
        const size_t added = pathable_edit::addCategory(
            state,
            new_category_name,
            new_category_display_name,
            new_category_description,
            new_category_copy_attributes);
        if (added == pack::kNoIndex) {
            status = "Could not add that category: check the name and display name aren't empty, that everything before "
                     "the name's last segment names an existing category, and that the id isn't already taken.";
        } else {
            status = "Added category " + state.pack.categories[added].id;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

// No menu bar: this one is a strip of controls, and a menu bar over it would be taller
// than its contents.
void Editor::drawToolbarWindow() {
    if (!beginWindow(ui::windows::Id::Toolbar, ImVec2(460, 120), false)) return;

    ImGui::TextUnformatted("Gizmo");
    ImGui::SameLine();

    int mode = static_cast<int>(state.settings.gizmo);
    ImGui::RadioButton("Off", &mode, static_cast<int>(editor::GizmoMode::Disabled));
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

void Editor::drawTrailEditorWindow() {
    if (!beginWindow(ui::windows::Id::TrailEditor, ImVec2(420, 560), true)) return;
    ui::trail_editor::draw(state, trail_view, frame_camera);
    ImGui::End();
}

void Editor::drawResourcesWindow() {
    if (!beginWindow(ui::windows::Id::Resources, ImVec2(420, 620), true)) return;

    // The window cannot open the dialog itself: one dialog runs at a time and its result is
    // polled here, so the request comes back out and is issued from the one place that
    // knows what the in-flight dialog is for.
    if (resource_window.draw(state) && !dialog.isPending()) {
        dialog_purpose = DialogPurpose::AddResource;
        dialog.requestOpenFile(imageFolder(), {{"Image", "png,jpg,jpeg,bmp"}});
    }

    ImGui::End();
}

// Undo and redo are bound only while the editor runs: a bind Nexus has a handler for is not
// the game's, and Ctrl+Z is the game's while nothing is being edited. Deregistering keeps the
// player's own choice of key for the next start.
void Editor::bindHistory(bool bound) {
    if (bound) {
        // The bindings everyone already has in their fingers, as the defaults.
        api->InputBinds.RegisterWithString(KEYBIND_UNDO_ID, onEditKeybind, "CTRL+Z");
        api->InputBinds.RegisterWithString(KEYBIND_REDO_ID, onEditKeybind, "CTRL+Y");
        api->Localization.Set(KEYBIND_UNDO_ID, "en", "Undo (Markus Editor)");
        api->Localization.Set(KEYBIND_REDO_ID, "en", "Redo (Markus Editor)");
    } else {
        api->InputBinds.Deregister(KEYBIND_UNDO_ID);
        api->InputBinds.Deregister(KEYBIND_REDO_ID);
    }
}

// Open Editor. Registers no render callback, so it is safe from inside one.
void Editor::start() {
    if (!active) {
        if (!settings_loaded) {
            settings_file.load(&state.settings, &windows, &last_folder, &last_image_folder);
            settings_loaded = true;
        }
        bindHistory(true);
        active = true;
    }
    windows[ui::windows::Id::Tree] = true;
}

// With nothing open and nothing on screen, nothing is left to edit: the editor lets go of its
// binds, and the viewer of anything it was drawing from it.
void Editor::stop() {
    if (!active) return;
    active = false;
    picking = false;
    mouse_over_ui = false;
    bindHistory(false);
    undo_requested = false;
    redo_requested = false;
    pending_select = pack::kNoIndex;
    pending_select_point = pack::kNoIndex;
    pending_deselect = false;
    world::overlay::clearHovered();
    closePack();
    edited_live.clear();
}

// What onWndProc reads of this frame on the message thread.
void Editor::publish() {
    picking = state.loaded && state.settings.select_in_viewport;
    mouse_over_ui = ImGui::GetIO().WantCaptureMouse;
}

void Editor::takeDialogResult() {
    const std::optional<std::filesystem::path> picked = dialog.takeResult();
    if (!picked) return;

    switch (dialog_purpose) {
    case DialogPurpose::SaveAs:
        // Save As always produces a zip, even from a folder pack: that is the only
        // form Blish HUD loads reliably.
        savePackTo(*picked, false);
        break;
    case DialogPurpose::AddResource:
        last_image_folder = picked->parent_path();
        resource_window.addFile(state, *picked, &status);
        break;
    case DialogPurpose::ExportTrail: ui::trail_editor::exportTo(state, *picked, &status); break;
    case DialogPurpose::OpenFile:
    case DialogPurpose::OpenFolder: openPack(*picked); break;
    }
}

// The clicks onWndProc saw on the world since the last frame.
void Editor::applyPicks(bool gizmo_active) {
    const size_t picked = pending_select.exchange(pack::kNoIndex);
    const size_t picked_point = pending_select_point.exchange(pack::kNoIndex);
    const bool deselect = pending_deselect.exchange(false);

    // ImGuizmo::IsOver() is only knowable on the frame, and a click on the gizmo is aimed at
    // what is already selected, not at the empty world or the marker behind it.
    if (gizmo_active) return;

    if (deselect) deselectInViewport(state);

    if (picked_point != pack::kNoIndex && picked_point < trail_edit::pointCount(state)) {
        // Straight into the point selection: the trail it belongs to keeps whatever standing
        // it had in the tree, and in the details window.
        editor::applyClick(state.points.indices, picked_point, viewportMods());
    } else if (picked != pack::kNoIndex && picked < state.pack.pathables.size()) {
        const editor::NodeRef node{editor::NodeKind::Pathable, picked};
        editor::applyClick(state.selection, node, viewportMods());
        // Picking in the world is only half the job: the marker has to be findable in the
        // tree afterwards, which for a deep category means opening the way down to it.
        if (state.selection.contains(node)) state.requestReveal(node);
    }
}

void Editor::render() {
    if (!active) return;
    takeDialogResult();

    undo::beginFrame(state);
    applyRequestedHistory();
    // Compares and writes only when something moved, so this is free on almost every frame.
    settings_file.update(state.settings, windows, last_folder, last_image_folder);

    // Outside the window body, which does not run while hidden or collapsed.
    if (windows[ui::windows::Id::TrailEditor]) {
        trail_edit::followSelection(state);
    } else {
        trail_edit::close(state);
    }

    // ImGui's display size, not NexusLinkData's — see gameCamera. The gizmo draws into an ImGui
    // draw list, so that is the space the projection has to land in.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const viewer::Camera camera = viewer::gameCamera(mumble_link, viewer::rtapi::data(), glm::vec2(display.x, display.y));
    frame_camera = camera;
    // The pack, and the overlay over it, only while the world is on screen; otherwise nothing
    // may stay hovered for a click to pick.
    if (!state.loaded || !camera.valid || camera.map_open) world::overlay::clearHovered();
    drawPack(camera);

    // After the markers so it draws over them, and so it can claim the click first.
    applyPicks(world::gizmo::draw(state, camera));

    // After the picks above, so a world pick reveals its marker in the same frame, and
    // outside drawTreeWindow, so a hidden or collapsed tree still consumes the request.
    const ui::tree_window::Reveal reveal = ui::tree_window::update(state);

    // Around the windows alone: the overlay and the gizmo above draw into the background list,
    // which a pushed font breaks (viewer::ui::fonts::push).
    viewer::ui::fonts::push();
    drawTreeWindow(reveal);
    // The others belong to the main window, and are drawn only with it.
    if (windows[ui::windows::Id::Tree]) {
        drawDetailsWindow();
        drawToolbarWindow();
        drawTrailEditorWindow();
        drawResourcesWindow();
    }
    viewer::ui::fonts::pop();

    applyPendingAction();
    publish();

    // A dialog's answer still has somewhere to go, so it keeps the editor running.
    if (!windows[ui::windows::Id::Tree] && !dialog.isPending()) stop();
}

// Between load and unload. The hooks are plain function pointers, so they reach it through here.
std::unique_ptr<Editor> editor;

void startEditor() {
    if (editor) editor->start();
}

void renderEditor() {
    if (editor) editor->render();
}

viewer::runtime::LiveMap* editedLive(int map_id, viewer::TextureLookup* texture) {
    return editor ? editor->editedLive(map_id, texture) : nullptr;
}

} // namespace

bool running() {
    return active.load();
}

ui::windows::Shown& windows() {
    return editor->windows;
}

viewer::EditorHooks hooks() {
    viewer::EditorHooks hooks;
    hooks.start = startEditor;
    hooks.render = renderEditor;
    hooks.wnd_proc = onWndProc;
    hooks.edited_live = editedLive;
    return hooks;
}

void load(AddonAPI* api, Mumble::Data* mumble_link) {
    editor = std::make_unique<Editor>(api, mumble_link);
}

void unload() {
    editor.reset();
}

} // namespace marker_editor
