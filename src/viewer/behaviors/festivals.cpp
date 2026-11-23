#include "viewer/behaviors/festivals.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

#include "viewer/http.h"

namespace viewer::behaviors {

const Festival kFestivals[6] = {
    {"halloween", "Halloween", 79},
    {"wintersday", "Wintersday", 98},
    {"superadventurefestival", "Super Adventure Festival", 162},
    {"lunarnewyear", "Lunar New Year", 201},
    {"festivalofthefourwinds", "Festival of the Four Winds", 213},
    {"dragonbash", "Dragon Bash", 233},
};

namespace {

// FestivalContext.DAILY_GROUP_ID: the daily achievements, whose categories include a
// festival's while it is on.
constexpr const char* kDailyGroupUrl = "https://api.guildwars2.com/v2/achievements/groups/18DB115A-8637-4290-A636-821362A3C4A8";

std::string lowercase(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

} // namespace

const char* festivalDisplayName(std::string_view name) {
    for (const Festival& festival : kFestivals) {
        if (lowercase(std::string(name)) == festival.name) return festival.display_name;
    }
    return "Unknown";
}

std::optional<std::vector<std::string>> festivalsFromDailyGroup(std::string_view json) {
    const nlohmann::json body = nlohmann::json::parse(json, nullptr, false);
    if (!body.is_object() || !body.contains("categories") || !body["categories"].is_array()) return std::nullopt;
    std::vector<std::string> active;
    for (const nlohmann::json& category : body["categories"]) {
        if (!category.is_number_integer()) continue;
        for (const Festival& festival : kFestivals) {
            if (category.get<int>() == festival.daily_category) active.push_back(festival.name);
        }
    }
    return active;
}

std::optional<std::vector<std::string>> festivalsFromList(std::string_view json) {
    const nlohmann::json body = nlohmann::json::parse(json, nullptr, false);
    if (!body.is_array()) return std::nullopt;
    std::vector<std::string> active;
    for (const nlohmann::json& name : body) {
        if (name.is_string()) active.push_back(lowercase(name.get<std::string>()));
    }
    return active;
}

void FestivalFetcher::start() {
    request_.start([](const http::Cancel& cancel) -> std::optional<std::vector<std::string>> {
        std::optional<std::vector<std::string>> active;
        if (const std::optional<std::string> body = http::get(kDailyGroupUrl, cancel)) active = festivalsFromDailyGroup(*body);
        // Since early 2022 the API has at times listed every festival at once; Blish HUD
        // asks its own service then, as it does when the API cannot be reached.
        if (active && active->size() != std::size(kFestivals)) return active;
        if (const std::optional<std::string> body = http::get("https://l.blishhud.com/general/getactivefestivals", cancel))
            return festivalsFromList(*body);
        return std::nullopt;
    });
}

const std::vector<std::string>& FestivalFetcher::active() const {
    static const std::vector<std::string> kNone;
    const std::vector<std::string>* active = request_.get();
    return active ? *active : kNone;
}

} // namespace viewer::behaviors
