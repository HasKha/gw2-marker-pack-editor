#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "http/background.h"
#include "viewer/mumble.h"

// Where a point of the world lands on the minimap and on the full map: Pathing's MapStates
// and MapProjection, in screen pixels.
//
// Three spaces. The world, MumbleLink's metres. The continent, the map's own coordinates, in
// which MumbleLink gives the map's centre: about two feet to a unit, y down. And UI pixels,
// what MumbleLink measures the compass in and what MapScale is continent units per: screen
// pixels at the Normal interface size, which the interface size and DPI scaling then scale.
//
// Pathing works in Blish HUD's own pixels instead, which are not the game's: it multiplies
// MapScale by 0.897 to make up for it, and stretches the compass by 19 to 40 of them
// (FlatMap.GetOffset). Both are the same correction, Normal's 0.897 in Blish HUD's
// GetScaleRatio, and come out here as UI pixels.
namespace viewer::map {

// Where the catalog keeps the maps' rectangles between sessions, in the addon's folder.
constexpr const char* CACHE_FILE_NAME = "maps.json";

// A map's rectangles, as /v2/maps gives them: where the map sits on its continent, and the
// world inside it in inches. MapStates.MapDetails.
struct MapDetails {
    glm::dvec2 continent_top_left{0.0}; // y down
    glm::dvec2 continent_size{0.0};
    glm::dvec2 map_top_left{0.0}; // inches; y up, so this is the map's highest y
    glm::dvec2 map_size{0.0};
};

using MapTable = std::unordered_map<int, MapDetails>;

// Every map in the body of /v2/maps?ids=all. Nothing when the body is not that.
std::optional<MapTable> mapsFromApi(std::string_view json);

// The maps in our cache, written in the layout of Pathing's ref/fallback/mapDetails.json —
// keyed by id, its MapDetails' property names — so either file reads as the other.
std::optional<MapTable> mapsFromCache(std::string_view json);
std::string mapsToCache(const MapTable& maps);

// Where `position`, in MumbleLink's metres, lies on its map's continent:
// MapStates.EventCoordsToMapCoords. Only x and z matter.
glm::dvec2 continentPosition(const MapDetails& map, const glm::vec3& position);

// UI pixels to screen pixels: Nexus's `Scaling`, computed with the DPI scale the game draws
// with (`dpi_scale`, 1 with its DPI Scaling off) in place of Nexus's own option. The interface
// size's factors are Nexus's, which Blish HUD's GetScaleRatio over its Normal agrees with to
// under a percent, and a `display` smaller than 1024 × 768 shrinks it as Nexus does.
float uiScale(int ui_size, float dpi_scale, glm::vec2 display);

// How the map on screen is drawn this frame: the compass in its corner, or the full map over
// the whole screen.
struct View {
    bool full_map = false;
    glm::vec2 min{0.0f}; // the bounds, in screen pixels: nothing outside is drawn
    glm::vec2 max{0.0f};

    glm::dvec2 map_centre{0.0}; // continent: what the bounds' middle shows
    double map_scale = 1.0; // MumbleLink's: continent units per UI pixel
    float ui_scale = 1.0f; // screen pixels per UI pixel
    double pixels_per_unit = 1.0; // screen pixels per continent unit

    // The compass turns with the camera when the player has it do so; the full map never does.
    bool rotate = false;
    float cos = 1.0f;
    float sin = 0.0f;

    bool contains(const glm::vec2& point) const { return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y; }
};

// The view MumbleLink describes on a `display` of that many screen pixels, with `ui_scale`
// screen pixels per UI pixel. Nothing before the game has written the compass's size, as
// Pathing's FlatMap keeps no bounds until then.
std::optional<View> view(const MumbleState& mumble, glm::vec2 display, float ui_scale);

// Where continent point `continent` is drawn: MapProjection.Project.
glm::vec2 project(const View& view, const glm::dvec2& continent);

// Every map's rectangles, from the cache at load and from the API once it answers, which then
// replaces the cache. Pathing asks the API at every start and falls back on its file; asking
// once a session and keeping the answer is the same, with the file written by us.
class Catalog
{
public:
    // The body of /v2/maps?ids=all, or nothing. Blocks: it runs on the worker.
    using Request = std::function<std::optional<std::string>(const http::Cancel& cancel)>;
    static std::optional<std::string> askApi(const http::Cancel& cancel);

    // Reads `cache_file` and sends `request` on a worker, writing what it answers to the
    // cache. An empty path keeps nothing.
    void start(const std::filesystem::path& cache_file, Request request = askApi);
    // Waits for the request, which its timeouts bound.
    void stop() { fresh_.stop(); }

    // The map's rectangles, or null for a map neither the API nor the cache knows.
    const MapDetails* find(int map_id) const;

private:
    MapTable cached_;
    http::Background<MapTable> fresh_;
};

} // namespace viewer::map
