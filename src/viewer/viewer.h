#pragma once

#include <Mumble.h>
#include <Nexus.h>

#include <filesystem>
#include <memory>
#include <string>

#include "pack/trl.h"
#include "viewer/camera.h"
#include "viewer/edited.h"
#include "viewer/scene.h"

// The viewer's lifecycle, and the addon-wide things it owns: the name, the folder, and the
// one options page Nexus shows for the addon.
namespace viewer {

constexpr const char* ADDON_NAME = "Marker Pack Editor";
// Nexus resolves this to <game>/addons/MarkerPackEditor and creates it. Both the viewer's
// settings and the editor's live inside, and our own packs in its `packs` folder.
constexpr const char* ADDON_FOLDER = "MarkerPackEditor";
constexpr const char* PACKS_FOLDER = "packs";

// The bind that opens the menu, which our icon in Nexus's bar presses when clicked. Its id is
// the one the Markers window's bind had, so a bind the player set carries over.
constexpr const char* KEYBIND_MARKERS_ID = "KB_MarkerPackEditor_Markers";
// The menu's items in Nexus's own menu, our icon in Nexus's bar, the same items behind the icon,
// and the icon's two textures.
constexpr const char* QUICKACCESS_ID = "QA_MarkerPackEditor_Markers";
constexpr const char* SHORTCUT_ID = "QA_MarkerPackEditor_Icon";
constexpr const char* SHORTCUT_MENU_ID = "QA_MarkerPackEditor_IconMenu";
constexpr const char* ICON_TEXTURE_ID = "TEX_MarkerPackEditor_Icon";
constexpr const char* ICON_HOVER_TEXTURE_ID = "TEX_MarkerPackEditor_IconHover";

// What the viewer reaches the editor through. entry.cpp connects them when editing is on;
// with editing off every hook stays null and the viewer never calls into the editor.
struct EditorHooks {
    // The editor's part of the options page, drawn under the viewer's own. Nexus shows one
    // options callback per addon — the first it finds registered from the addon's module
    // (CAddonsWindow in Nexus's UI/Views/MainWindow/Addons/Addons.cpp) — so the editor cannot
    // register one of its own beside the viewer's.
    void (*draw_options)() = nullptr;

    // Shows the editor's main window: the menu's Edit Marker Pack.
    void (*open)() = nullptr;

    // The editor's reads of its open pack's files, which see what is not saved yet: the edited
    // pack is drawn with these. Null draws it without images or trails.
    ImTextureID (*texture)(const std::string& path) = nullptr;
    std::shared_ptr<const pack::trl::Trail> (*trail)(const std::string& path, bool* deferred) = nullptr;

    // What only an editor draws over the world — the outline of what is selected, a trail's
    // point handles, a dot where an icon is missing — and what it picks, from `scene`, the
    // scene just drawn, under `camera`. Called at Nexus's render, over the world, while the
    // world is on screen.
    void (*draw_overlay)(const Camera& camera, const Scene& scene) = nullptr;
};

void load(AddonAPI* api, Mumble::Data* mumble_link);
void unload();

// Whether this session runs the editor: the "Enable pack editing" setting as it stood at
// load. Ticking the setting changes what the next load does, not this one — see drawOptions
// in viewer.cpp.
bool editingEnabled();

void connectEditor(const EditorHooks& hooks);

// The pack the editor has open. Call every frame, after the frame's edits: the viewer compares,
// so opening, closing, reloading and Save As all land without a call of their own.
//
// The viewer draws that pack from the editor's model instead of from disk, and closes the
// installed pack at its path, because the editor saves a zip by renaming over it and Windows
// refuses that while any handle is open. Once the editor lets go, the pack is read again, if it
// was one of the installed packs.
void setEditedPack(const edited::Report& report);

} // namespace viewer
