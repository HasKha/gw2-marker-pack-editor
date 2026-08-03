#pragma once

#include <Mumble.h>
#include <Nexus.h>

namespace marker_editor {

constexpr const char* ADDON_NAME = "Marker Pack Editor";
constexpr const char* SETTINGS_FOLDER = "MarkerPackEditor";
constexpr const char* SETTINGS_FILE = "MarkerPackEditor/settings.json";
constexpr const char* QUICKACCESS_ID = "QA_MarkerPackEditor";
constexpr const char* KEYBIND_TOGGLE_ID = "KB_MarkerPackEditor_Toggle";
constexpr const char* KEYBIND_UNDO_ID = "KB_MarkerPackEditor_Undo";
constexpr const char* KEYBIND_REDO_ID = "KB_MarkerPackEditor_Redo";

void load(AddonAPI* api, NexusLinkData* nexus_link, Mumble::Data* mumble_link);
void unload();

} // namespace marker_editor
