#pragma once

#include <Mumble.h>
#include <Nexus.h>

#include "viewer/live/runtime.h"
#include "viewer/render/scene.h"

// The viewer's lifecycle, and the addon-wide things it owns: the name, the folder, and the
// one options page Nexus shows for the addon.
namespace viewer {

constexpr const char* ADDON_NAME = "Markus";
// Nexus resolves this to <game>/addons/Markus and creates it. Both the viewer's
// settings and the editor's live inside, and our own packs in its `packs` folder.
constexpr const char* ADDON_FOLDER = "Markus";
// The same folder under the addon's old name, which releases up to 1.6 wrote to. load() moves
// it to ADDON_FOLDER once; see store::adoptFolder.
constexpr const char* OLD_ADDON_FOLDER = "MarkerPackEditor";
constexpr const char* PACKS_FOLDER = "packs";

// The ids below are Nexus's, and Nexus keeps a player's binds and bar layout under them, so
// they still carry the addon's old name: renaming them would drop those. Nexus shows the
// names Localization.Set gives them, never the ids.
//
// The bind our icon in Nexus's bar presses for a left click, which opens the menu.
constexpr const char* KEYBIND_MENU_ID = "KB_MarkerPackEditor_Markers";
// Turns All Markers on or off. Unset unless the player sets it.
constexpr const char* KEYBIND_ALL_MARKERS_ID = "KB_MarkerPackEditor_AllMarkers";
// The menu's items in Nexus's own menu, our icon in Nexus's bar, the same items behind the icon,
// and the icon's two textures.
constexpr const char* QUICKACCESS_ID = "QA_MarkerPackEditor_Markers";
constexpr const char* SHORTCUT_ID = "QA_MarkerPackEditor_Icon";
constexpr const char* SHORTCUT_MENU_ID = "QA_MarkerPackEditor_IconMenu";
constexpr const char* ICON_TEXTURE_ID = "TEX_MarkerPackEditor_Icon";
constexpr const char* ICON_HOVER_TEXTURE_ID = "TEX_MarkerPackEditor_IconHover";

// What the viewer reaches the editor through. entry.cpp connects them before the viewer loads;
// with no editor every hook stays null and the viewer never calls into one.
struct EditorHooks {
    // Starts the editor, or shows its main window when it runs: the menu's Open Editor.
    void (*start)() = nullptr;

    // The editor's frame and its share of the game's messages, from the viewer's own: Nexus
    // holds its render mutex across every callback, so a render callback registered when the
    // editor starts would have to be registered from inside one. Both do nothing while the
    // editor does not run. A message the editor returns 0 for is the editor's.
    void (*render)() = nullptr;
    UINT (*wnd_proc)(HWND window, UINT message, WPARAM w_param, LPARAM l_param) = nullptr;

    // The pack open in the editor, which the viewer draws with the installed ones — sorted
    // with them, through the same sinks and options — so that it looks as it will once
    // installed: its live map brought up to date for `map_id`, and how its images are read,
    // which sees images not saved yet. Null while no pack is open. Called at Present, before
    // the scene is built.
    runtime::LiveMap* (*edited_live)(int map_id, TextureLookup* texture) = nullptr;
};

void load(AddonAPI* api, Mumble::Data* mumble_link);
void unload();

// Before load: the viewer's callbacks reach the editor as soon as they are registered.
void connectEditor(const EditorHooks& hooks);

// What bounds how much of every image is handed to Nexus at once, the editor's included: one
// Nexus makes them all at one Present. The viewer moves it on a frame at a time.
TextureUploads& textureUploads();

} // namespace viewer
