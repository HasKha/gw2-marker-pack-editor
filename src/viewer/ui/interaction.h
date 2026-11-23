#pragma once

#include <string>

#include "viewer/behaviors/update.h"
#include "viewer/settings.h"

// What behaviors show on screen: a focused marker's `info`, and the prompt saying what the
// interact key will do — Pathing's InfoWindow and SmallInteract, as plain ImGui windows where
// Pathing draws the game's own window art and gear icon.
namespace viewer::ui::interaction {

// Draws the last info string as `info` says, and the prompt when `show_prompt`, its "{0}"
// replaced by `key_name`. True when the prompt was clicked, which interacts as the key does.
bool draw(const behaviors::Screen& screen, settings::InfoDisplay info, const std::string& key_name, bool show_prompt);

} // namespace viewer::ui::interaction
