#pragma once

#include <Nexus.h>

#include <filesystem>

#include "editor/editor_state.h"

// The handful of things that should survive a game restart, in
// `<addons>/MarkerPackEditor/settings.json`.
//
// Deliberately not in here: anything about the *pack*. Visibility toggles, the selection
// and the open trail all belong to a pack, not to the editor, and a settings file that
// remembered them would be wrong the moment a different pack was opened.
namespace settings {

void init(AddonAPI* api);

// Applies whatever was saved. Call once, before the first frame. A missing or unreadable
// file is not an error — it just means the defaults stand.
void load(editor::Settings* editor, std::filesystem::path* last_folder, std::filesystem::path* last_image_folder);

// Writes the file when anything it persists has changed since the last write. Call once a
// frame: it compares rather than being told, so a new setting cannot be added and then
// forgotten at the call sites that change it.
void update(const editor::Settings& editor, const std::filesystem::path& last_folder, const std::filesystem::path& last_image_folder);

} // namespace settings
