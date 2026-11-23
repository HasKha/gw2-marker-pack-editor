#pragma once

#include "viewer/runtime.h"
#include "viewer/script/engine.h"

// What scripts show the player: their console, and the entries they add to the menu.
namespace viewer::ui::script_console {

// The "Script console" window while `*open`: messages, Debug:Watch values, each pack's
// script time per frame, and the census of what scripts asked for that does not exist.
void draw(const script::Engine& engine, const runtime::Installed& installed, bool* open);

// The entries scripts added under Pathing's "Scripts" menu, as a tree; nothing when there are
// none. Drawn inside whatever window holds it.
void drawMenu(script::Engine* engine);

} // namespace viewer::ui::script_console
