#pragma once

#include <string>

#include "viewer/behaviors/update.h"

// What behaviors show on screen: a focused marker's `info`, and the prompt saying what the
// interact key will do — Pathing's InfoWindow and SmallInteract, as plain ImGui windows. Their
// look, and the gear Pathing draws, come with the rest of the screen in milestone 4.
namespace viewer::ui::interaction {

// Draws the last info string, and the prompt when `show_prompt`, its "{0}" replaced by
// `key_name`. True when the prompt was clicked, which interacts as the key does.
bool draw(const behaviors::Screen& screen, const std::string& key_name, bool show_prompt);

} // namespace viewer::ui::interaction
