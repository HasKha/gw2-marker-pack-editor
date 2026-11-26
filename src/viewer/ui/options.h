#pragma once

#include <cstddef>
#include <string>

#include <imgui.h>

#include "viewer/settings.h"

// The parts of the options page that are Pathing's settings, by its own names. Each returns
// true when it changed a setting that is saved.
namespace viewer::ui::options {

// Where the menu is shown: Nexus's right-click menu, and an icon in Nexus's bar.
bool drawInterface(settings::Settings* settings);

// What the player asked of the Marker Packs section. The section draws one button per action,
// beside `status`, how many packs are in or how loading goes.
enum class PacksAction { None, ScriptsToggled, Manage, Reload, Import };
PacksAction drawPacks(settings::Settings* settings, const std::string& status);

// The slow-frame log's switch and threshold, in the Development section.
bool drawSlowFrameLog(settings::Settings* settings);

// Pathing's pack settings: how the world is drawn, and what behaviors may do and put on
// screen. `show_hidden` is its "Temporarily Show Ghost Markers", for this session only and
// never saved.
bool drawPack(settings::Settings* settings, bool* show_hidden);

// Pathing's map settings: those both maps share, and what each shows under its own name.
// `above` and `below` are the images the maps mark a far icon with, shown in their checkbox's
// label; either may be null.
bool drawMaps(settings::Settings* settings, ImTextureID above, ImTextureID below);

// What the player asked of the API key section.
enum class KeyAction { None, OpenApplications, Register, Remove, ClearCache };

// The API key section: how a key is made, then the field to paste one into and Register, or,
// with `registered` set, that key as asterisks and Remove; and a button that clears what is
// cached from the API. `field` is what the player has
// typed, `status` the last check's outcome, empty for none; while `checking`, Register does
// nothing.
KeyAction drawApiKey(const std::string& registered, std::string* field, bool checking, const std::string& status);

} // namespace viewer::ui::options
