#pragma once

#include "viewer/runtime.h"
#include "viewer/script/engine.h"

// What scripts show the player: their console, and the entries they add to the menu.
namespace viewer::ui::script_console {

// The "Script console" window while `*open`: messages, Debug:Watch values, each pack's
// script time per frame, and the census of what scripts asked for that does not exist.
void draw(const script::Engine& engine, const runtime::Installed& installed, bool* open);

} // namespace viewer::ui::script_console
