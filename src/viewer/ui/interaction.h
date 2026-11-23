#pragma once

#include <string>

#include "viewer/achievements.h"
#include "viewer/behaviors/update.h"
#include "viewer/settings.h"

// What behaviors show on screen: a focused marker's `info`, and the prompt saying what the
// interact key will do — Pathing's InfoWindow and SmallInteract, as plain ImGui windows where
// Pathing draws the game's own window art and gear icon.
namespace viewer::ui::interaction {

// Draws the last info string as `info` says, and the prompt when `show_prompt`, its "{0}"
// replaced by `key_name` — over an achievement's details, from `achievements`, for an
// achievement marker's gear. True when the prompt was clicked, which interacts as the key does.
bool draw(
    const behaviors::Screen& screen,
    settings::InfoDisplay info,
    const std::string& key_name,
    bool show_prompt,
    achievements::Catalog* achievements);

} // namespace viewer::ui::interaction
