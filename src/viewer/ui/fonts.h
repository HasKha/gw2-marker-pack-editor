#pragma once

#include <string>

struct ID3D11Device;

// Font Awesome in our windows, so an icon is text: fonts::label(ICON_FA_TRASH, "Delete").
//
// A copy of the host's window font with the icons merged into it, in an ImGui atlas of our own
// uploaded to the game's device. Not added to Nexus's atlas: Nexus builds every font it is handed
// with its own glyph ranges, which leave the icons out. The font and its licence are in assets/fonts/.
namespace viewer::ui::fonts {

// Keeps the device the atlas is uploaded to. Nothing is built until the first push(). Without a
// device the windows keep the host's font, and label() leaves the icons out.
void load(ID3D11Device* device);

void unload();

// Makes the windows' font, with the icons, the current font, at the size of the current one,
// until pop(). Pushed before ImGui::Begin(), it holds for the title bar too. The first push builds
// it from the current font: Nexus's, as the player picked it then; after that only its size is
// followed, until the addon is reloaded.
//
// **Around windows only, never around drawing into the background or foreground list.** Pushing
// a font points ImGui's shared draw data — the white pixel every line and filled shape takes its
// colour from — into our atlas, but switches the texture of the current window's list alone. A
// list that is no window's keeps sampling Nexus's atlas, at our atlas's coordinates: the map's
// trails drawn that way were invisible, and its icons, being images, were not.
void push();
void pop();

// Whether the icons draw: the font is built.
bool ready();

// "<icon> <text>" while the icons draw, `text` alone otherwise: before the first push, or with no
// device to build on.
std::string label(const char* icon, const char* text);

// Why the font is not built, or what it was built from.
const char* status();

} // namespace viewer::ui::fonts
