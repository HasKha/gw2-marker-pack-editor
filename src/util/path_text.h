#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Paths to text and back, in UTF-8.
//
// path::string() and a path built from a std::string go through the ANSI code page, and under
// MSVC string() throws std::system_error for a character that code page cannot hold: a pack
// named in Hungarian on a Western system, or in Japanese on any other. A pack's name reaches
// labels, log lines and comparisons from inside a render callback or a loader thread, where a
// throw takes the game down, so text about a path is made with these.
namespace util {

// `path` as UTF-8, for a label, a log line or a comparison.
inline std::string pathText(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// The path UTF-8 `text` names: the catalog's file names, and anything pathText wrote.
inline std::filesystem::path pathFromText(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace util
