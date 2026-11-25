#pragma once

#include <cstdint>
#include <vector>

// The start of a PNG of `width` × `height`, RGBA: as far as stb's header read goes, which stops at
// the first IDAT chunk and checks no CRC. That read is all the texture cache does with an image
// before Nexus decodes it, so a fake Nexus can take these for the image itself.
inline std::vector<uint8_t> pngHeader(uint32_t width, uint32_t height) {
    const auto be32 = [](std::vector<uint8_t>& out, uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(value >> shift));
    };
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    be32(png, 13);
    png.insert(png.end(), {'I', 'H', 'D', 'R'});
    be32(png, width);
    be32(png, height);
    png.insert(png.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA, no interlace
    be32(png, 0); // CRC, unchecked
    be32(png, 0);
    png.insert(png.end(), {'I', 'D', 'A', 'T'});
    return png;
}

// The size a pngHeader() says, as a fake Nexus that never decodes reads it back.
inline bool pngSize(const uint8_t* bytes, size_t size, uint32_t* width, uint32_t* height) {
    if (size < 24) return false;
    const auto be32 = [&](size_t at) {
        return (uint32_t{bytes[at]} << 24) | (uint32_t{bytes[at + 1]} << 16) | (uint32_t{bytes[at + 2]} << 8) | bytes[at + 3];
    };
    *width = be32(16);
    *height = be32(20);
    return true;
}
