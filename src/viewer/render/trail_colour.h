#pragma once

#include <imgui.h>

#include <cstdint>
#include <vector>

// The colour a trail without a tint is drawn with on the minimap and the full map: the dominant
// colour of its texture, as Pathing picks it (TextureResourceManager.SampleColor), so a yellow
// trail is yellow on the map too.
//
// Pathing quantizes the texture with its port of ColorThief, a median cut, and takes the most
// populous of four colours. Its port is followed to the letter, its slips included, because what
// it picks is the colour: a different quantizer, however good, picks another shade of the same
// texture. `trail_colour.cpp` says where each of those slips is.
namespace viewer::trail_colour {

// What Pathing draws a trail with when its texture gives no colour: XNA's Color.DarkGray. A
// texture that will not read or decode, or whose every pixel is transparent or white, gets this.
constexpr ImU32 NO_COLOUR = IM_COL32(169, 169, 169, 255);

// The dominant colour of `width` × `height` pixels, RGBA with straight alpha, row by row: the
// pixels are premultiplied first, as Blish HUD loads every texture. Opaque.
ImU32 dominant(const uint8_t* rgba, int width, int height);

// The dominant colour of an image file's bytes, decoded as MonoGame decodes Pathing's.
ImU32 sample(const std::vector<uint8_t>& file);

} // namespace viewer::trail_colour
