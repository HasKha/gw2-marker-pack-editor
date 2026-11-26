#pragma once

#include <Mumble.h>
#include <Nexus.h>

#include "editor/ui/windows.h"
#include "viewer/viewer.h"

namespace marker_editor {

// Registered only while the editor runs — see bindHistory.
constexpr const char* KEYBIND_UNDO_ID = "KB_MarkerPackEditor_Undo";
constexpr const char* KEYBIND_REDO_ID = "KB_MarkerPackEditor_Redo";

// The editor runs on demand. load keeps what it is given and registers nothing with Nexus; the
// viewer drives the editor through hooks(), and the editor runs from the menu's Edit Marker
// Pack until it has no pack open and none of its windows on screen.
void load(AddonAPI* api, Mumble::Data* mumble_link);
// Drops what is open, saved or not.
void unload();

// Whether the editor runs now.
bool running();

// Which of the editor's windows are on screen, as the View menu toggles them. Between load and
// unload only.
ui::windows::Shown& windows();

// What the viewer drives the editor through: starting it, its frame and its share of the game's
// messages. Nothing else passes between them.
viewer::EditorHooks hooks();

} // namespace marker_editor
