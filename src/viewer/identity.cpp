#include "viewer/identity.h"

#include <windows.h>

#include <nlohmann/json.hpp>

namespace viewer {

namespace {

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

template <class T>
void readNumber(const nlohmann::json& object, const char* key, T* out) {
    const auto it = object.find(key);
    if (it != object.end() && it->is_number()) *out = it->get<T>();
}

} // namespace

const Identity& parseIdentity(const wchar_t* json) {
    static std::wstring cached_text;
    static Identity cached;

    const wchar_t* text = json ? json : L"";
    if (cached_text == text) return cached;
    cached_text = text;
    cached = Identity{};

    const nlohmann::json parsed = nlohmann::json::parse(toUtf8(cached_text), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return cached;

    if (const auto name = parsed.find("name"); name != parsed.end() && name->is_string()) cached.name = name->get<std::string>();
    readNumber(parsed, "profession", &cached.profession);
    readNumber(parsed, "spec", &cached.specialization);
    readNumber(parsed, "race", &cached.race);
    readNumber(parsed, "map_id", &cached.map_id);
    readNumber(parsed, "world_id", &cached.world_id);
    readNumber(parsed, "team_color_id", &cached.team_color_id);
    readNumber(parsed, "fov", &cached.fov);
    readNumber(parsed, "uisz", &cached.ui_size);
    if (const auto commander = parsed.find("commander"); commander != parsed.end() && commander->is_boolean()) {
        cached.commander = commander->get<bool>();
    }
    return cached;
}

} // namespace viewer
