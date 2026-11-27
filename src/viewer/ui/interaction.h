#pragma once

#include <imgui.h>

#include <string>

#include "viewer/behaviors/update.h"
#include "viewer/gw2_api/achievements.h"
#include "viewer/settings.h"

// What behaviors show on screen: a focused marker's `info`, and the gear saying what the interact
// key will do — Pathing's InfoWindow and SmallInteract, as ImGui windows. The info box is plain
// where Pathing draws the game's window art; the gear is the game's own.
namespace viewer::ui::interaction {

struct Drawn {
    bool prompt_clicked = false; // interacts as the key does
    bool info_moved = false; // the info box was let go after a drag: `settings` holds its new place
};

// Draws the last info string as `settings` says, closed into `screen->info_closed` by its X and,
// when movable, dragged into `settings`' place for it; and the prompt when the settings show it:
// the `gear` image at `ui_scale` screen pixels per UI pixel, its text a tooltip with "{0}"
// replaced by `key_name` — under an achievement's details, from `achievements`, for an
// achievement marker's gear. A null `gear` is not drawn but still takes the mouse.
Drawn draw(
    behaviors::Screen* screen,
    settings::Settings* settings,
    float ui_scale,
    ImTextureID gear,
    const std::string& key_name,
    achievements::Catalog* achievements);

} // namespace viewer::ui::interaction
