#include "viewer/game/identity.h"

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

Identity parseIdentity(const wchar_t* json) {
    Identity identity;
    const nlohmann::json parsed = nlohmann::json::parse(toUtf8(json ? json : L""), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return identity;

    if (const auto name = parsed.find("name"); name != parsed.end() && name->is_string()) identity.name = name->get<std::string>();
    readNumber(parsed, "profession", &identity.profession);
    readNumber(parsed, "spec", &identity.specialization);
    readNumber(parsed, "race", &identity.race);
    readNumber(parsed, "map_id", &identity.map_id);
    readNumber(parsed, "world_id", &identity.world_id);
    readNumber(parsed, "team_color_id", &identity.team_color_id);
    readNumber(parsed, "fov", &identity.fov);
    readNumber(parsed, "uisz", &identity.ui_size);
    if (const auto commander = parsed.find("commander"); commander != parsed.end() && commander->is_boolean()) {
        identity.commander = commander->get<bool>();
    }
    return identity;
}

} // namespace viewer
