#pragma once

#include <imgui.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "viewer/game/mumble.h"
#include "viewer/live/runtime.h"
#include "viewer/maps/map.h"
#include "viewer/render/scene.h"

// What the minimap and the full map show: Pathing's FlatMap and each pathable's
// RenderToMiniMap, decided at Present and drawn into whichever draw list the frame has.
//
// **In continent space, with no view.** As the world scene carries no camera, this carries
// no MapScale, centre or rotation: those are read from MumbleLink where it is drawn, so a
// map being dragged does not trail a frame behind. What is decided here is what MumbleLink
// moving does not change within a frame — what shows, in what colour, in what order.
//
// **Two scenes while the full map is up**: the minimap's, under the map as it opens and closes
// over the compass, and the full map's. MumbleLink's compass fields describe the full map then,
// so the minimap's scene carries the compass as it last was (`MapScene::compass`).
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
    bool above_below = false;
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

// What MumbleLink says of the compass. While the full map is up the same fields describe the full
// map: CompassKeeper keeps the compass's.
struct Compass {
    int width = 0;
    int height = 0;
    double rotation = 0.0;
    bool rotating = false;
    bool top_right = false;
    double scale = 1.0;
    glm::dvec2 centre{0.0};
};
Compass compassOf(const MumbleState& mumble);
// `mumble` with `compass` in its compass fields and the full map down.
MumbleState withCompass(MumbleState mumble, const Compass& compass);

// The compass the minimap is drawn on, decided at Present. MumbleLink's compass fields and its
// IsMapOpen do not change on the same frame: seen in a recording, November 2026, a frame with
// IsMapOpen off still had the full map's fields, and drew the minimap at the full map's scale.
// So the compass is kept from the full map coming up until the fields are no longer the ones the
// full map last had.
class CompassKeeper
{
public:
    struct Kept {
        Compass compass;
        bool keep = false; // MumbleLink's fields are not the compass's: draw on `compass` whatever they say
    };
    Kept update(const MumbleState& mumble);

private:
    Compass compass_;
    Compass full_map_;
    bool keep_ = false;
};

struct MapScene {
    bool full_map = false; // which map it was decided for: the map's settings differ
    float ui_scale = 1.0f; // screen pixels per UI pixel, which the view is built with
    float opacity = 1.0f; // the map's opacity under its fade, for every line and icon
    // The minimap's: the compass it was built on, drawn on whenever MumbleLink has the full map up
    // as the scene is drawn, or `keep_compass` says its fields are not the compass's. A moment can
    // read MumbleLink after it changed and before the next Present built a scene for it.
    Compass compass;
    bool keep_compass = false;
    // The full map's, while it opens or closes: how many times larger than MumbleLink's view the
    // game draws its map (FullMapZoom). 1 once it is still.
    float zoom = 1.0f;
    float trail_width = 2.0f;
    bool fade_far_trails = true;
    std::vector<std::shared_ptr<const TrailLines>> trails; // each layer's
    std::vector<Path> paths; // the trails that show, drawn first, under every icon
    std::vector<Icon> icons; // farthest from the camera first, as FlatMap sorts them
    // The game's above and below images, drawn on an icon's corner; none drawn while null.
    ImTextureID above = nullptr;
    ImTextureID below = nullptr;

    // What building it works in, kept with it so that a recycled scene builds without allocating.
    // Never read once it is built.
    struct Workspace {
        std::vector<DepthKey> keys; // the icons' sort
        std::vector<Icon> sorted; // the icons in that order, swapped in for them
    };
    Workspace workspace;
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

// The game's full map opening and closing. It draws the map at the view MumbleLink describes and
// scales the whole picture, its icons too, about the screen's centre, linearly in the scale:
// opening, down to 1 a set time after IsMapOpen comes on; closing, up from 1 a little after the
// world comes back, while it fades out. The figures, and how they were measured, are beside them
// in map_scene.cpp.
//
// IsMapOpen only goes off well after the closing ends, so the closing is told by the world
// instead: the game stops drawing it once the map is fully open, and GoodRender's moments stop
// with it; they come back on the closing's first frame. Without GoodRender, or when the map
// closes before it was fully open, the closing is not seen and the map stays still until
// IsMapOpen goes off.
class FullMapZoom
{
public:
    struct Frame {
        float zoom = 1.0f; // MapScene::zoom
        float opacity = 1.0f; // the closing's fade, under FadeIn's
    };

    // At Present: whether MumbleLink has the full map up, whether a GoodRender moment came for the
    // world in the frame before (good_render_sink::Drawn::world), and the time.
    Frame update(bool map_open, bool world_drawn, double seconds);

private:
    bool map_open_ = false;
    double opened_at_ = 0.0;
    int frames_without_world_ = 0;
    std::optional<double> closing_at_;
};

// One live map going onto the map.
struct Layer {
    const runtime::LiveMap* live = nullptr;
    const std::vector<char>* visible = nullptr; // runtime::visibleNodes over its nodes
    TextureLookup texture;
    TrailCache* trails = nullptr; // its own: a cache keeps one live map's lines
    // The pack open in the editor, whose cache drops an image whose file changes: its images are
    // looked up every frame rather than kept (LiveMarker::image).
    bool edited = false;
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
// view's bounds by hand: a GoodRender list ignores clip rectangles. Then scaled by the scene's
// zoom about the bounds' middle, as the game scales its own map's picture.
void draw(ImDrawList* list, const MapScene& scene, const map::View& view, float player_height);

// The icon under `mouse`, the nearest one drawn last; null when none is.
const Icon* hovered(const MapScene& scene, const map::View& view, glm::vec2 mouse);

// What the map's tooltip says of a marker: FlatMap.UpdateTooltip. Empty `title` for none.
struct Tooltip {
    std::string title;
    std::string description;
};

// The tip name and description, the categories it sits in, short, and how far away, above or below it is; with
// `category_path`, the whole category chain instead, as Pathing shows with Shift held.
Tooltip tooltip(const runtime::LiveMarker& marker, const registry::CategoryTree& tree, const glm::vec3& player, bool category_path);

// The top left corner of a tooltip `size` big for the mouse at `mouse`, on a `display` that
// size. The game's cursor hangs down and right of its tip, by a size nothing reports and many
// players make large, so the tooltip keeps to where no cursor reaches: above and right of the
// tip, else left of it, never under it.
glm::vec2 tooltipPosition(glm::vec2 mouse, glm::vec2 size, glm::vec2 display);

// The scenes the sinks draw, each one atomic swap of a finished one, as the world's is: the
// minimap's always, the full map's while it is up and null otherwise.
//
// They are drawn in different places. The minimap goes under the game's windows at GoodRender's
// BEFORE_WINDOWS, so under the full map as it opens and closes over it. The full map goes over
// everything, in the overlay at Nexus's render: the game draws it after BEFORE_WINDOWS, and draws
// no world while it is fully open, so no moment fires then at all.
void publish(std::shared_ptr<const MapScene> minimap, std::shared_ptr<const MapScene> full_map);
std::shared_ptr<const MapScene> published(bool full_map);

// Draws the published minimap or full map under the view MumbleLink holds now, on a `display`
// that size: the minimap on its scene's compass while the full map is up, the full map only
// while MumbleLink has it up. Any thread: MumbleLink is read without its identity.
void drawPublished(ImDrawList* list, const Mumble::Data* mumble, glm::vec2 display, bool full_map);

} // namespace viewer::map_scene
