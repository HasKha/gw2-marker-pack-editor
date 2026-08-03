#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace pack::trl {

// A run of consecutive points. A .trl holds one or more of these, separated by a (0,0,0)
// point, which TmfLib treats as a terminator rather than a coordinate.
struct Segment {
    std::vector<glm::vec3> points;

    // How many (0,0,0) points followed this segment in the file. Normally 0 or 1, but
    // real packs contain longer runs. Kept so write() reproduces the file exactly, for
    // the same reason attribute text is kept verbatim: never dirty what wasn't edited.
    uint32_t terminators_after = 0;
};

struct Trail {
    int32_t map_id = 0;

    // Terminators before the first real point. Rare, but they occur.
    uint32_t leading_terminators = 0;

    std::vector<Segment> segments;

    size_t pointCount() const;
};

// Layout (TmfLib/Reader/TrlFileReader.cs, corroborated by gw2-markers/scripts/trltools.py):
//   int32   version   -- must be 0; anything else is a format we do not know
//   int32   map_id
//   float32 x, y, z   -- repeated to EOF, same order and space as xpos/ypos/zpos
//
// A file shorter than the 8-byte header yields an empty trail rather than an error, and a
// trailing partial point is ignored -- both match what the reference reader does.
bool read(const std::vector<uint8_t>& data, Trail* out, std::string* error);

// Writes the layout above, re-emitting a (0,0,0) terminator between segments.
std::vector<uint8_t> write(const Trail& trail);

} // namespace pack::trl
