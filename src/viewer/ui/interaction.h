#pragma once

#include <string>

#include "viewer/behaviors/update.h"
#include "viewer/gw2_api/achievements.h"
#include "viewer/settings.h"

// What behaviors show on screen: a focused marker's `info`, and the prompt saying what the
// interact key will do — Pathing's InfoWindow and SmallInteract, as plain ImGui windows where
// Pathing draws the game's own window art and gear icon.
namespace viewer::ui::interaction {

struct Drawn {
    bool prompt_clicked = false; // interacts as the key does
    bool info_moved = false; // the info box was let go after a drag: `settings` holds its new place
};

// Draws the last info string as `settings` says, closed into `screen->info_closed` by its X and,
// when movable, dragged into `settings`' place for it; and the prompt when the settings show it,
// its "{0}" replaced by `key_name` — over an achievement's details, from `achievements`, for an
// achievement marker's gear.
Drawn draw(behaviors::Screen* screen, settings::Settings* settings, const std::string& key_name, achievements::Catalog* achievements);

} // namespace viewer::ui::interaction
