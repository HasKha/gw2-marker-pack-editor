#pragma once

#include <filesystem>
#include <string>

#include "editor_state.h"
#include "world/camera.h"

namespace ui::trail_editor {

// What the window asked marker_editor to do this frame. The native file dialog is owned
// there — one runs at a time and its result is polled from the render callback — so a
// request comes back out rather than being started here.
enum class Request { None, Export };

// Draws the trail open in the editor into the current window: its map id, its points, and
// drag fields for the selected one.
Request draw(editor::State& state, const world::Camera& camera);

// The Trail menu. Drawn only in this window's own menu bar — it acts on the trail editor's
// state, so it has no business in the tree or the details window. Greyed out when no trail
// is open.
Request drawMenu(editor::State& state, const world::Camera& camera);

// Writes the working copy to a .trl the user picked.
void exportTo(editor::State& state, const std::filesystem::path& file, std::string* status);

} // namespace ui::trail_editor
