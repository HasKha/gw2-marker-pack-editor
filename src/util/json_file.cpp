#include "util/json_file.h"

#include <windows.h>

#include <fstream>

namespace util::json_file {

std::optional<nlohmann::json> readObject(const std::filesystem::path& file) {
    if (file.empty()) return std::nullopt;

    std::ifstream input(file);
    if (!input) return std::nullopt;

    nlohmann::json parsed = nlohmann::json::parse(input, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return std::nullopt;
    return parsed;
}

bool writeText(const std::filesystem::path& file, std::string_view text) {
    if (file.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    std::filesystem::path temporary = file;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();

    // Write-through, so the move is not on disk ahead of the bytes it puts in place.
    if (output && MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    std::filesystem::remove(temporary, ec);
    return false;
}

bool write(const std::filesystem::path& file, const nlohmann::json& value) {
    return writeText(file, value.dump(2) + "\n");
}

bool readBool(const nlohmann::json& object, const char* key, bool fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

int readInt(const nlohmann::json& object, const char* key, int fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

float readFloat(const nlohmann::json& object, const char* key, float fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<float>() : fallback;
}

std::string readString(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

} // namespace util::json_file
