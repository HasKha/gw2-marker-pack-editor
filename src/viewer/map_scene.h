#pragma once

#include <imgui.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "viewer/map.h"
#include "viewer/mumble.h"
#include "viewer/runtime.h"
#include "viewer/scene.h"

// What the minimap and the full map show: Pathing's FlatMap and each pathable's
// RenderToMiniMap, decided at Present and drawn into whichever draw list the frame has.
//
// **In continent space, with no view.** As the world scene carries no camera, this carries
// no MapScale, centre or rotation: those are read from MumbleLink where it is drawn, so a
// map being dragged does not trail a frame behind. What is decided here is what MumbleLink
// moving does not change within a frame — what shows, in what colour, in what order.
//
// **Trails are laid on the continent once per live map**, not per frame: a map with every
// corpus pack installed has 180,000 trail points. A frame picks which trails show; the fade by
// height, which follows the player, is worked out where they are drawn.
namespace viewer::map_scene {

// Pathing's MapVisibilityLevel: Default follows each pathable's mapVisibility or
// miniMapVisibility; Always and Never override it.
enum class Visibility { Default, Always, Never };

struct Options {
    bool enabled = true; // "Show Markers on Maps"
    Visibility map_markers = Visibility::Default;
    Visibility map_trails = Visibility::Default;
    Visibility minimap_markers = Visibility::Default;
    Visibility minimap_trails = Visibility::Default;
    float map_opacity = 1.0f;
    float minimap_opacity = 1.0f;
    bool above_below = true;
    bool fade_far_trails = true;
    float trail_width = 2.0f; // Blish HUD's pixels
    bool show_hidden = false; // what behaviors filter is drawn too
};

// A marker on the map.
struct Icon {
    size_t layer = 0; // which of the layers the scene was built from it is in
    size_t marker = 0; // into that layer's LiveMap::markers
    ImTextureID texture = nullptr;
    ImU32 colour = IM_COL32_WHITE; // tint, alpha and the map's opacity
    glm::dvec2 continent{0.0};
    float size = 20.0f; // mapDisplaySize
    bool scale_with_zoom = true;
    int above_below = 0; // +1 well above the player, -1 well below, 0 neither or not shown
    float depth = 0.0f; // squared distance from the camera it was sorted by
};

// One stretch of a trail between two of its points.
struct Line {
    glm::dvec2 from{0.0};
    glm::dvec2 to{0.0};
    float height = 0.0f; // metres: the middle of the stretch, for the fade by height
};

// One trail's lines, and the continent box around them, to skip a trail off the map whole.
struct Path {
    size_t layer = 0; // whose TrailLines: MapScene::trails[layer]
    size_t first = 0; // into TrailLines::lines
    size_t count = 0;
    glm::dvec2 min{0.0};
    glm::dvec2 max{0.0};
    ImU32 colour = IM_COL32_WHITE; // the trail's sample colour, before any opacity
};

// Every trail of a live map, on the continent: one Path per LiveMap::trails, in that order.
struct TrailLines {
    std::vector<Line> lines;
    std::vector<Path> paths;
};

// Lays a live map's trails on the continent, and keeps them while its trails are the same.
class TrailCache
{
public:
    std::shared_ptr<const TrailLines> get(const runtime::LiveMap& live, const map::MapDetails& details);

private:
    int map_id_ = 0;
    uint64_t revision_ = 0;
    size_t trail_count_ = 0;
    const map::MapDetails* details_ = nullptr;
    std::shared_ptr<const TrailLines> lines_;
};

struct MapScene {
    bool full_map = false; // which map it was decided for: the map's settings differ
    float ui_scale = 1.0f; // screen pixels per UI pixel, which the view is built with
    float opacity = 1.0f; // the map's opacity under its fade, for every line and icon
    float trail_width = 2.0f;
    bool fade_far_trails = true;
    std::vector<std::shared_ptr<const TrailLines>> trails; // each layer's
    std::vector<Path> paths; // the trails that show, drawn first, under every icon
    std::vector<Icon> icons; // farthest from the camera first, as FlatMap sorts them
};

// Pathing's fade after the map opens or closes, or the interface is resized: 0.65 s from
// nothing to the map's opacity.
class FadeIn
{
public:
    // The fade at `seconds`, moving the start when the map or the interface changes.
    float update(const MumbleState& mumble, double seconds);

private:
    bool map_open_ = false;
    int ui_size_ = -1;
    double since_ = 0.0;
};

// One live map going onto the map.
struct Layer {
    const runtime::LiveMap* live = nullptr;
    const std::vector<char>* visible = nullptr; // runtime::visibleNodes over its nodes
    TextureLookup texture;
    TrailCache* trails = nullptr; // its own: a cache keeps one live map's lines
};

// Builds what the map shows of every layer on `details`' map, for the map MumbleLink has up:
// each layer's trails, then every icon sorted together. `opacity` is the fade-in; `ui_scale`
// how many screen pixels a UI pixel is.
void build(
    const std::vector<Layer>& layers,
    const map::MapDetails& details,
    const MumbleState& mumble,
    const Options& options,
    float opacity,
    float ui_scale,
    MapScene* out);

// The same over one live map, `visible` being runtime::visibleNodes and `trails` what keeps
// its lines from one frame to the next.
void build(
    const runtime::LiveMap& live,
    const std::vector<char>& visible,
    const map::MapDetails& details,
    const MumbleState& mumble,
    const Options& options,
    float opacity,
    float ui_scale,
    const TextureLookup& texture,
    TrailCache* trails,
    MapScene* out);

// How opaque `line` is drawn, the player at `player_height` metres: the scene's opacity, and
// with fade_far_trails half a percent less per metre between them, to no less than 0.15
// (StandardTrail.MiniMap.DrawSegment).
float lineOpacity(const MapScene& scene, const Line& line, float player_height);

// Where `icon` is drawn under `view`, as a box in screen pixels: Pathing's draw rectangle,
// which is also what the mouse is over. False when its centre is off the map, which Pathing
// draws nothing of.
bool iconRect(const Icon& icon, const map::View& view, glm::vec2* min, glm::vec2* max);

// Draws `scene` into `list` under `view`, the player at `player_height` metres, cut to the
// view's bounds by hand: a GoodRender list ignores clip rectangles.
void draw(ImDrawList* list, const MapScene& scene, const map::View& view, float player_height);

// The icon under `mouse`, the nearest one drawn last; null when none is.
const Icon* hovered(const MapScene& scene, const map::View& view, glm::vec2 mouse);

// What the map's tooltip says of a marker: FlatMap.UpdateTooltip. Empty `title` for none.
struct Tooltip {
    std::string title;
    std::string description;
};

// The tip name and description, the categories it sits in, short, and how far away it is; with
// `category_path`, the whole category chain instead, as Pathing shows with Shift held.
Tooltip tooltip(const runtime::LiveMarker& marker, const registry::CategoryTree& tree, const glm::vec3& player, bool category_path);

// The top left corner of a tooltip `size` big for the mouse at `mouse`, on a `display` that
// size. The game's cursor hangs down and right of its tip, by a size nothing reports and many
// players make large, so the tooltip keeps to where no cursor reaches: above and right of the
// tip, else left of it, never under it.
glm::vec2 tooltipPosition(glm::vec2 mouse, glm::vec2 size, glm::vec2 display);

// The scene the sinks draw, one atomic swap of a finished one, as the world's is.
void publish(std::shared_ptr<const MapScene> scene);
std::shared_ptr<const MapScene> published();

// Draws the published scene under the view MumbleLink holds now, on a `display` that size.
// Any thread: MumbleLink is read without its identity.
void drawPublished(ImDrawList* list, const Mumble::Data* mumble, glm::vec2 display);

} // namespace viewer::map_scene
