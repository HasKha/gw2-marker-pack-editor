#pragma once

#include <Nexus.h>

// The viewer's lifecycle, and the addon-wide things it owns: the name, the folder, and the
// one options page Nexus shows for the addon.
namespace viewer {

constexpr const char* ADDON_NAME = "Marker Pack Editor";
// Nexus resolves this to <game>/addons/MarkerPackEditor and creates it. Both the viewer's
// settings and the editor's live inside.
constexpr const char* ADDON_FOLDER = "MarkerPackEditor";

// What the viewer reaches the editor through. entry.cpp connects them when editing is on;
// with editing off every hook stays null and the viewer never calls into the editor.
struct EditorHooks {
    // The editor's part of the options page, drawn under the viewer's own. Nexus shows one
    // options callback per addon — the first it finds registered from the addon's module
    // (CAddonsWindow in Nexus's UI/Views/MainWindow/Addons/Addons.cpp) — so the editor cannot
    // register one of its own beside the viewer's.
    void (*draw_options)() = nullptr;
};

void load(AddonAPI* api);
void unload();

// Whether this session runs the editor: the "Enable pack editing" setting as it stood at
// load. Ticking the setting changes what the next load does, not this one — see drawOptions
// in viewer.cpp.
bool editingEnabled();

void connectEditor(const EditorHooks& hooks);

} // namespace viewer
