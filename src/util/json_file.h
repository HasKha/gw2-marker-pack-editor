#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Small JSON files the addon keeps between sessions, read leniently.
//
// These are exactly the kind of file that gets hand-edited or half-written, and they are
// read from inside a render callback. A bad value leaves the default standing; it never
// takes the addon down.
namespace util::json_file {

// The JSON object in `file`. Nothing when the file is missing, will not parse, or holds
// something other than an object: in every case the defaults stand and the next write
// replaces it.
std::optional<nlohmann::json> readObject(const std::filesystem::path& file);

// Writes `text` to `file`, creating its folder first, so that the file is the old text or the
// new and never part of either: into `<file>.tmp`, then moved over it. The files kept between
// sessions are written every few seconds, and a game that crashes mid-write is common enough
// that a truncated file — every category the player turned off back on — would be seen. False
// when any of that fails, with the old file standing.
bool writeText(const std::filesystem::path& file, std::string_view text);

// `value` as these files hold it, with any string in it that is not UTF-8 repaired rather
// than thrown on.
std::string text(const nlohmann::json& value);

// writeText of text(value).
bool write(const std::filesystem::path& file, const nlohmann::json& value);

// One member of `object`, or `fallback` when it is missing or of another type. nlohmann's own
// value() throws on the wrong type.
bool readBool(const nlohmann::json& object, const char* key, bool fallback);
int readInt(const nlohmann::json& object, const char* key, int fallback);
float readFloat(const nlohmann::json& object, const char* key, float fallback); // any number
std::string readString(const nlohmann::json& object, const char* key);

} // namespace util::json_file
