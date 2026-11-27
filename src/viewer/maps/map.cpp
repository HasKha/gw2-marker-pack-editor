#include "viewer/maps/map.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

#include "http/http.h"

namespace viewer::map {

namespace {

// MapStates.METERCONVERSION: inches to the metre, as a float, as Pathing has it.
constexpr double kInchesPerMetre = 39.37f;

// FlatMap's 40 of Blish HUD's pixels between the bottom of the screen and a compass in the
// bottom corner, which leave the experience bar showing: 40 × 0.897 UI pixels.
constexpr float kCompassBottomMargin = 40.0f * 0.897f;

// [[x1, y1], [x2, y2]], or nothing.
std::optional<std::pair<glm::dvec2, glm::dvec2>> readRect(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array() || it->size() != 2) return std::nullopt;
    glm::dvec2 corners[2];
    for (int i = 0; i < 2; ++i) {
        const nlohmann::json& corner = (*it)[i];
        if (!corner.is_array() || corner.size() != 2 || !corner[0].is_number() || !corner[1].is_number()) return std::nullopt;
        corners[i] = glm::dvec2(corner[0].get<double>(), corner[1].get<double>());
    }
    return std::pair{corners[0], corners[1]};
}

std::optional<double> readNumber(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) return std::nullopt;
    return it->get<double>();
}

std::string readFile(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

void writeFile(const std::filesystem::path& file, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

} // namespace

std::optional<MapTable> mapsFromApi(std::string_view json) {
    const nlohmann::json body = nlohmann::json::parse(json, nullptr, false);
    if (!body.is_array()) return std::nullopt;
    MapTable maps;
    for (const nlohmann::json& map : body) {
        if (!map.is_object() || !map.contains("id") || !map["id"].is_number_integer()) continue;
        const auto continent = readRect(map, "continent_rect");
        const auto world = readRect(map, "map_rect");
        if (!continent || !world) continue;
        // Gw2Sharp's Rectangle, which Pathing reads them through: continent_rect runs from
        // its top left, y down; map_rect from its bottom left, y up, so its top is the
        // second corner's y.
        MapDetails details;
        details.continent_top_left = continent->first;
        details.continent_size = continent->second - continent->first;
        details.map_top_left = glm::dvec2(world->first.x, world->second.y);
        details.map_size = world->second - world->first;
        maps[map["id"].get<int>()] = details;
    }
    return maps;
}

std::optional<MapTable> mapsFromCache(std::string_view json) {
    const nlohmann::json body = nlohmann::json::parse(json, nullptr, false);
    if (!body.is_object()) return std::nullopt;
    MapTable maps;
    for (const auto& [key, value] : body.items()) {
        if (!value.is_object()) continue;
        int id = 0;
        try {
            id = std::stoi(key);
        } catch (const std::exception&) {
            continue;
        }
        const auto field = [&value](const char* name) { return readNumber(value, name); };
        const auto cx = field("ContinentRectTopLeftX"), cy = field("ContinentRectTopLeftY");
        const auto cw = field("ContinentRectWidth"), ch = field("ContinentRectHeight");
        const auto mx = field("MapRectTopLeftX"), my = field("MapRectTopLeftY");
        const auto mw = field("MapRectWidth"), mh = field("MapRectHeight");
        if (!cx || !cy || !cw || !ch || !mx || !my || !mw || !mh) continue;
        maps[id] = MapDetails{{*cx, *cy}, {*cw, *ch}, {*mx, *my}, {*mw, *mh}};
    }
    return maps;
}

std::string mapsToCache(const MapTable& maps) {
    nlohmann::json body = nlohmann::json::object();
    for (const auto& [id, map] : maps) {
        body[std::to_string(id)] = {
            {"ContinentRectTopLeftX", map.continent_top_left.x},
            {"ContinentRectTopLeftY", map.continent_top_left.y},
            {"MapRectTopLeftX", map.map_top_left.x},
            {"MapRectTopLeftY", map.map_top_left.y},
            {"MapRectWidth", map.map_size.x},
            {"MapRectHeight", map.map_size.y},
            {"ContinentRectWidth", map.continent_size.x},
            {"ContinentRectHeight", map.continent_size.y},
        };
    }
    return body.dump();
}

glm::dvec2 continentPosition(const MapDetails& map, const glm::vec3& position) {
    // Blish HUD's X and Y, which are MumbleLink's x and z.
    const double east = position.x * kInchesPerMetre;
    const double north = position.z * kInchesPerMetre;
    return glm::dvec2(
        map.continent_top_left.x + (east - map.map_top_left.x) / map.map_size.x * map.continent_size.x,
        map.continent_top_left.y - (north - map.map_top_left.y) / map.map_size.y * map.continent_size.y);
}

float uiScale(int ui_size, float dpi_scale, glm::vec2 display) {
    // Small, Normal, Large, Larger.
    static constexpr float kScales[] = {0.90f, 1.00f, 1.11f, 1.22f};
    const float size = ui_size >= 0 && ui_size < static_cast<int>(std::size(kScales)) ? kScales[ui_size] : 1.0f;
    // CScaling::UpdateResolution.
    const float resolution = std::min(std::min(display.x, 1024.0f) / 1024.0f, std::min(display.y, 768.0f) / 768.0f);
    return size * (dpi_scale > 0.0f ? dpi_scale : 1.0f) * (resolution > 0.0f ? resolution : 1.0f);
}

std::optional<View> view(const MumbleState& mumble, glm::vec2 display, float ui_scale) {
    if (!mumble.available || mumble.compass_width < 1 || mumble.compass_height < 1 || mumble.map_scale <= 0.0) return std::nullopt;

    View result;
    result.full_map = mumble.is_map_open;
    if (result.full_map) {
        result.max = display;
    } else {
        const glm::vec2 size = glm::vec2(mumble.compass_width, mumble.compass_height) * ui_scale;
        const float bottom = mumble.is_compass_top_right ? size.y : display.y - kCompassBottomMargin * ui_scale;
        result.min = glm::vec2(display.x - size.x, bottom - size.y);
        result.max = glm::vec2(display.x, bottom);
    }

    result.map_centre = mumble.map_center;
    result.map_scale = mumble.map_scale;
    result.ui_scale = ui_scale;
    result.pixels_per_unit = ui_scale / mumble.map_scale;

    result.rotate = !mumble.is_map_open && mumble.is_compass_rotation_enabled;
    if (result.rotate) {
        // As Matrix.CreateRotationZ takes it: a float.
        const float radians = static_cast<float>(mumble.compass_rotation);
        result.cos = std::cos(radians);
        result.sin = std::sin(radians);
    }
    return result;
}

glm::vec2 project(const View& view, const glm::dvec2& continent) {
    const glm::dvec2 offset = (continent - view.map_centre) * view.pixels_per_unit;
    glm::vec2 location(static_cast<float>(offset.x), static_cast<float>(offset.y));
    if (view.rotate) {
        location = glm::vec2(location.x * view.cos - location.y * view.sin, location.x * view.sin + location.y * view.cos);
    }
    return location + (view.min + view.max) * 0.5f;
}

std::optional<std::string> Catalog::askApi(const http::Cancel& cancel) {
    // A few hundred kilobytes: longer than the festivals' five seconds.
    return http::get("https://api.guildwars2.com/v2/maps?ids=all", cancel, std::chrono::seconds(15));
}

void Catalog::start(const std::filesystem::path& cache_file, Request request) {
    if (!cache_file.empty()) {
        if (std::optional<MapTable> from_disk = mapsFromCache(readFile(cache_file))) cached = std::move(*from_disk);
    }
    fresh.start([cache_file, request = std::move(request)](const http::Cancel& cancel) -> std::optional<MapTable> {
        const std::optional<std::string> body = request(cancel);
        if (!body) return std::nullopt;
        std::optional<MapTable> maps = mapsFromApi(*body);
        if (!maps || maps->empty()) return std::nullopt;
        if (!cache_file.empty()) writeFile(cache_file, mapsToCache(*maps));
        return maps;
    });
}

const MapDetails* Catalog::find(int map_id) const {
    const MapTable* answered = fresh.get();
    const MapTable& maps = answered ? *answered : cached;
    const auto it = maps.find(map_id);
    return it == maps.end() ? nullptr : &it->second;
}

} // namespace viewer::map
