#pragma once

#include "viewer/settings.h"

// The parts of the options page that are Pathing's settings, by its own names. Each returns
// true when it changed a setting that is saved.
namespace viewer::ui::options {

// Pathing's pack settings: how the world is drawn, and what behaviors may do and put on
// screen. `show_hidden` is its "Temporarily Show Ghost Markers", for this session only and
// never saved.
bool drawPack(settings::Settings* settings, bool* show_hidden);

// Pathing's map settings.
bool drawMaps(settings::Settings* settings);

} // namespace viewer::ui::options
