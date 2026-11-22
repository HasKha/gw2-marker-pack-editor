#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

// Small JSON files the addon keeps between sessions, read leniently.
//
// These are exactly the kind of file that gets hand-edited or half-written, and they are
// read from inside a render callback. A bad value leaves the default standing; it never
// takes the addon down.
namespace viewer::json_file {

// The JSON object in `file`. Nothing when the file is missing, will not parse, or holds
// something other than an object: in every case the defaults stand and the next write
// replaces it.
std::optional<nlohmann::json> readObject(const std::filesystem::path& file);

// Writes `value` to `file`, creating its folder first. False when any of that fails.
bool write(const std::filesystem::path& file, const nlohmann::json& value);

// One member of `object`, or `fallback` when it is missing or of another type. nlohmann's own
// value() throws on the wrong type.
bool readBool(const nlohmann::json& object, const char* key, bool fallback);
int readInt(const nlohmann::json& object, const char* key, int fallback);
std::string readString(const nlohmann::json& object, const char* key);

} // namespace viewer::json_file
