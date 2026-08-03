#pragma once

#include <filesystem>
#include <string>

#include "editor_state.h"

namespace ui::resource_window {

// Draws the pack's images into the current window: a folder tree, what references each
// one, and a preview of the selected file.
//
// Returns true when the user asked to add a file. The native file dialog is owned by
// marker_editor — one runs at a time and it is polled from the render callback — so the
// request comes back out rather than being started here.
bool draw(editor::State& state);

// Brings `file` into the pack, under the folder the window is pointed at. Like every other
// change it is pending until the pack is saved.
void addFile(editor::State& state, const std::filesystem::path& file, std::string* status);

} // namespace ui::resource_window
