#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace pack {
struct Pack;
}

// Which characters text needs that our windows' font is not built with from the start, so that it
// is built with those and no more: a pack in Chinese uses a few thousand of CJK's twenty thousand,
// and an atlas of them all would be tens of megabytes, built twice over (fonts.*).
namespace viewer::ui::glyphs {

// A block of codepoints, first and last inclusive.
struct Block {
    char32_t first = 0;
    char32_t last = 0;
};

// What every build has: Latin-1 and Latin Extended-A and B, the ranges Nexus builds its fonts
// with, so the host's font covers them already.
constexpr Block kBase[] = {{0x0020, 0x00FF}, {0x0100, 0x024F}};

// A set of characters outside kBase. The Basic Multilingual Plane only, which is all ImGui 1.80's
// 16-bit ImWchar can draw; it holds every living script's common characters.
class Set
{
public:
    // Adds the characters of `utf8` beyond kBase; whether any of them was new. Skips what is
    // not drawn as a glyph: controls, the private use area (Font Awesome's icons are there),
    // anything beyond the plane, and bytes that are not UTF-8.
    bool add(std::string_view utf8);
    // The same for one character.
    bool add(char32_t c);
    // Whether any of `other` was new.
    bool add(const Set& other);

    bool contains(char32_t c) const;
    size_t size() const { return count; }
    bool empty() const { return count == 0; }

    // As ImGui's GlyphRanges: first and last of each run, ended by a 0. Only what lies inside
    // `within`, every block of it when empty. Just the 0 when nothing does.
    std::vector<uint16_t> ranges(std::span<const Block> within = {}) const;

private:
    bool insert(char32_t c);

    std::vector<uint64_t> words; // one bit a codepoint; sized at the first insert
    size_t count = 0;
};

// Every character a pack's text holds beyond kBase: its categories' names and every attribute
// value, a marker's tooltip among them. Paths and numbers come along and cost nothing.
Set of(const pack::Pack& pack);

} // namespace viewer::ui::glyphs
