#include "viewer/json_file.h"

#include <fstream>

namespace viewer::json_file {

std::optional<nlohmann::json> readObject(const std::filesystem::path& file) {
    if (file.empty()) return std::nullopt;

    std::ifstream input(file);
    if (!input) return std::nullopt;

    nlohmann::json parsed = nlohmann::json::parse(input, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return std::nullopt;
    return parsed;
}

bool write(const std::filesystem::path& file, const nlohmann::json& value) {
    if (file.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    std::ofstream output(file, std::ios::trunc);
    if (!output) return false;
    output << value.dump(2) << "\n";
    return static_cast<bool>(output);
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

} // namespace viewer::json_file
