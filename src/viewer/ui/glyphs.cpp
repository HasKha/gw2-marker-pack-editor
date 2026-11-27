#include "viewer/ui/glyphs.h"

#include <bit>

#include "pack/model.h"

namespace viewer::ui::glyphs {

namespace {

constexpr char32_t kPlaneEnd = 0x10000;

bool inBase(char32_t c) {
    for (const Block& block : kBase) {
        if (c >= block.first && c <= block.last) return true;
    }
    return false;
}

// Whether `c` is drawn as a glyph of a text font: not a control, a surrogate, the private use
// area or a noncharacter.
bool drawable(char32_t c) {
    if (c < 0x20 || (c >= 0x7F && c < 0xA0)) return false;
    if (c >= 0xD800 && c <= 0xF8FF) return false; // surrogates, then the private use area
    if (c >= 0xFFFE) return false;
    return true;
}

// The codepoint starting at `text[*at]`, moving `*at` past it. A byte that does not start a
// well-formed sequence is skipped alone and reads as 0, so the character after it still reads.
char32_t next(std::string_view text, size_t* at) {
    const auto byte = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(*at);
    size_t length = 0;
    char32_t c = 0;
    char32_t least = 0; // below it the sequence is overlong
    if (lead < 0x80) {
        ++*at;
        return lead;
    } else if ((lead & 0xE0) == 0xC0) {
        length = 2;
        c = lead & 0x1F;
        least = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        c = lead & 0x0F;
        least = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        c = lead & 0x07;
        least = 0x10000;
    } else {
        ++*at;
        return 0;
    }
    if (*at + length > text.size()) {
        ++*at;
        return 0;
    }
    for (size_t i = 1; i < length; ++i) {
        const unsigned char continuation = byte(*at + i);
        if ((continuation & 0xC0) != 0x80) {
            ++*at;
            return 0;
        }
        c = (c << 6) | (continuation & 0x3F);
    }
    *at += length;
    return c < least || c > 0x10FFFF ? 0 : c;
}

} // namespace

bool Set::add(std::string_view utf8) {
    bool added = false;
    for (size_t at = 0; at < utf8.size();) {
        // Most text in most packs is ASCII, which is all base.
        if (static_cast<unsigned char>(utf8[at]) < 0x80) {
            ++at;
            continue;
        }
        if (add(next(utf8, &at))) added = true;
    }
    return added;
}

bool Set::add(char32_t c) {
    return c < kPlaneEnd && drawable(c) && !inBase(c) && insert(c);
}

bool Set::add(const Set& other) {
    if (other.words_.empty()) return false;
    if (words_.empty()) words_.resize(other.words_.size());
    bool added = false;
    for (size_t i = 0; i < words_.size(); ++i) {
        const uint64_t fresh = other.words_[i] & ~words_[i];
        if (!fresh) continue;
        words_[i] |= fresh;
        size_ += static_cast<size_t>(std::popcount(fresh));
        added = true;
    }
    return added;
}

bool Set::contains(char32_t c) const {
    if (c >= kPlaneEnd || words_.empty()) return false;
    return (words_[c / 64] >> (c % 64)) & 1u;
}

bool Set::insert(char32_t c) {
    if (words_.empty()) words_.resize(kPlaneEnd / 64);
    uint64_t& word = words_[c / 64];
    const uint64_t bit = uint64_t{1} << (c % 64);
    if (word & bit) return false;
    word |= bit;
    ++size_;
    return true;
}

std::vector<uint16_t> Set::ranges(std::span<const Block> within) const {
    std::vector<uint16_t> out;
    const auto inside = [&](char32_t c) {
        if (within.empty()) return true;
        for (const Block& block : within) {
            if (c >= block.first && c <= block.last) return true;
        }
        return false;
    };
    if (!words_.empty()) {
        char32_t run = 0; // the first of the run being walked; 0 for none, which is never in the set
        for (char32_t c = 1; c <= kPlaneEnd; ++c) {
            const bool in = c < kPlaneEnd && contains(c) && inside(c);
            if (in && !run) run = c;
            if (!in && run) {
                out.push_back(static_cast<uint16_t>(run));
                out.push_back(static_cast<uint16_t>(c - 1));
                run = 0;
            }
        }
    }
    out.push_back(0);
    return out;
}

Set of(const pack::Pack& pack) {
    Set set;
    for (const pack::Category& category : pack.categories) {
        set.add(category.name);
        for (const pack::Attribute& attribute : category.attributes.all()) set.add(attribute.value);
    }
    for (const pack::Pathable& pathable : pack.pathables) {
        for (const pack::Attribute& attribute : pathable.attributes.all()) set.add(attribute.value);
    }
    return set;
}

} // namespace viewer::ui::glyphs
