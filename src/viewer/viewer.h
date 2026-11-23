#pragma once

#include <Mumble.h>
#include <Nexus.h>

// The viewer's lifecycle, and the addon-wide things it owns: the name, the folder, and the
// one options page Nexus shows for the addon.
namespace viewer {

constexpr const char* ADDON_NAME = "Marker Pack Editor";
// Nexus resolves this to <game>/addons/MarkerPackEditor and creates it. Both the viewer's
// settings and the editor's live inside, and our own packs in its `packs` folder.
constexpr const char* ADDON_FOLDER = "MarkerPackEditor";
constexpr const char* PACKS_FOLDER = "packs";

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
};

void load(AddonAPI* api, Mumble::Data* mumble_link);
void unload();

// Before load: the viewer's callbacks reach the editor as soon as they are registered.
void connectEditor(const EditorHooks& hooks);

} // namespace viewer
