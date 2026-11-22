#pragma once

#include <Mumble.h>
#include <Nexus.h>

#include <filesystem>

// The viewer's lifecycle, and the addon-wide things it owns: the name, the folder, and the
// one options page Nexus shows for the addon.
namespace viewer {

constexpr const char* ADDON_NAME = "Marker Pack Editor";
// Nexus resolves this to <game>/addons/MarkerPackEditor and creates it. Both the viewer's
// settings and the editor's live inside, and our own packs in its `packs` folder.
constexpr const char* ADDON_FOLDER = "MarkerPackEditor";
constexpr const char* PACKS_FOLDER = "packs";

constexpr const char* KEYBIND_MARKERS_ID = "KB_MarkerPackEditor_Markers";
constexpr const char* QUICKACCESS_ID = "QA_MarkerPackEditor_Markers";

// What the viewer reaches the editor through. entry.cpp connects them when editing is on;
// with editing off every hook stays null and the viewer never calls into the editor.
struct EditorHooks {
    // The editor's part of the options page, drawn under the viewer's own. Nexus shows one
    // options callback per addon — the first it finds registered from the addon's module
    // (CAddonsWindow in Nexus's UI/Views/MainWindow/Addons/Addons.cpp) — so the editor cannot
    // register one of its own beside the viewer's.
    void (*draw_options)() = nullptr;
};

void load(AddonAPI* api, Mumble::Data* mumble_link);
void unload();

// Whether this session runs the editor: the "Enable pack editing" setting as it stood at
// load. Ticking the setting changes what the next load does, not this one — see drawOptions
// in viewer.cpp.
bool editingEnabled();

void connectEditor(const EditorHooks& hooks);

// The pack the editor has open, empty when none. Call every frame: the viewer compares, so
// opening, closing, reloading and Save As all land without a call of their own.
//
// The viewer stops drawing that pack — the editor draws it — and closes its file, because
// the editor saves a zip by renaming over it and Windows refuses that while any handle is
// open. Once the editor lets go, the pack is read again, if it was one of the installed packs.
void setEditedPack(const std::filesystem::path& path);

} // namespace viewer
