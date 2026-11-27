#pragma once

#include <memory>
#include <string>

struct ID3D11Device;

// Font Awesome in our windows, so an icon is text: fonts::label(ICON_FA_TRASH, "Delete").
//
// A copy of the host's window font with the icons merged into it, in an ImGui atlas of our own
// uploaded to the game's device. Not added to Nexus's atlas: Nexus builds every font it is handed
// with its own glyph ranges, which leave the icons out. The font and its licence are in assets/fonts/.
//
// Text beyond Latin — Chinese, Korean, Cyrillic, and so on — is drawn with the host's font where it
// has the character and otherwise with one of Windows' own fonts (Segoe UI, Microsoft YaHei, Malgun
// Gothic, Segoe UI Symbol), built with only the characters require() was handed. A script that
// needs shaping, Arabic or Devanagari, draws its letters unjoined: ImGui 1.80 does not shape.
namespace viewer::ui::glyphs {
class Set;
}

namespace viewer::ui::fonts {

// The atlas and the fonts built into it, owned by the viewer's session. Current while it lives,
// as an ImGui context is: the free functions below draw with the current one, since windows all
// over the viewer and the editor call them, and with none they keep the host's font and leave the
// icons out.
class Fonts
{
public:
    Fonts();
    ~Fonts();

    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;

    // The device the atlas is uploaded to, which outlives this. Nothing is built until the first
    // push(). Without a device the windows keep the host's font.
    void setDevice(ID3D11Device* device);

    struct State;

private:
    std::unique_ptr<State> state_;
};

// Makes the windows' font, with the icons, the current font, at the size of the current one,
// until pop(). Pushed before ImGui::Begin(), it holds for the title bar too. The first push builds
// it from the current font: Nexus's, as the player picked it then; after that only its size is
// followed, until the addon is reloaded. A later push may build it again, from that same font
// file, with what require() and the field being typed into added since.
//
// **Around windows only, never around drawing into the background or foreground list.** Pushing
// a font points ImGui's shared draw data — the white pixel every line and filled shape takes its
// colour from — into our atlas, but switches the texture of the current window's list alone. A
// list that is no window's keeps sampling Nexus's atlas, at our atlas's coordinates: the map's
// trails drawn that way were invisible, and its icons, being images, were not.
void push();
void pop();

// Inside a push(), the same font larger, for a title in a list, until pop(). Without the icons.
// The current font while ours is not built.
void pushHeading();

// Builds the font with `glyphs` too, at a push() a while from now: the atlas is built again from
// the start, so arrivals close together share a build. On the render thread. Nothing without a
// current Fonts.
void require(const glyphs::Set& glyphs);

// Whether the icons draw: the font is built.
bool ready();

// "<icon> <text>" while the icons draw, `text` alone otherwise: before the first push, or with no
// device to build on.
std::string label(const char* icon, const char* text);

} // namespace viewer::ui::fonts
