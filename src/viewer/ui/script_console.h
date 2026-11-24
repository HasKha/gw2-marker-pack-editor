#pragma once

#include <filesystem>

#include "viewer/runtime.h"
#include "viewer/script/engine.h"

// What scripts show the player: their console, and the entries they add to the menu.
namespace viewer::ui::script_console {

// Where the window's Save writes the messages, in our folder; each save replaces the last.
constexpr const char* SAVE_FILE_NAME = "script_console.txt";

// The "Lua script console" window while `*open`: messages, Debug:Watch values, each pack's
// script time per frame, and the census of what scripts asked for that does not exist.
// The messages can be copied to the clipboard, or saved to `save_file` when there is one.
void draw(const script::Engine& engine, const runtime::Installed& installed, const std::filesystem::path& save_file, bool* open);

} // namespace viewer::ui::script_console
