#pragma once

#include <cstddef>
#include <string>

#include "viewer/settings.h"

// The parts of the options page that are Pathing's settings, by its own names. Each returns
// true when it changed a setting that is saved.
namespace viewer::ui::options {

// Pathing's pack settings: how the world is drawn, and what behaviors may do and put on
// screen. `show_hidden` is its "Temporarily Show Ghost Markers", for this session only and
// never saved.
bool drawPack(settings::Settings* settings, bool* show_hidden);

// Pathing's map settings, a section for each map. Those both maps share are in both.
bool drawMinimap(settings::Settings* settings);
bool drawFullMap(settings::Settings* settings);

// What the player asked of the API key section.
enum class KeyAction { None, OpenApplications, Register, Remove, ClearCache };

// The API key section: how a key is made, then the field to paste one into and Register, or,
// with `registered` set, that key as asterisks and Remove; and a button that clears what is
// cached from the API. `field` is what the player has
// typed, `status` the last check's outcome, empty for none; while `checking`, Register does
// nothing.
KeyAction drawApiKey(const std::string& registered, char* field, size_t field_size, bool checking, const std::string& status);

} // namespace viewer::ui::options
