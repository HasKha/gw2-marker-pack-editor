#pragma once

#include <Mumble.h>
#include <Nexus.h>

namespace marker_editor {

constexpr const char* QUICKACCESS_ID = "QA_MarkerPackEditor";
constexpr const char* KEYBIND_TOGGLE_ID = "KB_MarkerPackEditor_Toggle";
constexpr const char* KEYBIND_UNDO_ID = "KB_MarkerPackEditor_Undo";
constexpr const char* KEYBIND_REDO_ID = "KB_MarkerPackEditor_Redo";

void load(AddonAPI* api, Mumble::Data* mumble_link);
void unload();

// The editor's part of the addon's options page. Not registered with Nexus: the viewer owns
// the page and draws this under its own, through viewer::EditorHooks.
void drawOptions();

} // namespace marker_editor
