#include "viewer/map_scene.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "viewer/bake.h"

namespace viewer::map_scene {

namespace {

std::atomic<std::shared_ptr<const MapScene>> latest_minimap;
std::atomic<std::shared_ptr<const MapScene>> latest_full_map;

// Blish HUD's pixels per UI pixel at the Normal interface size (GraphicsService.GetScaleRatio),
// which Pathing's sizes on the map are in: an icon's mapDisplaySize, a trail's width.
constexpr float kBlishPixel = 0.897f;

// FlatMap's fade-in, in seconds. About the game's own as the full map opens: from the recording
// FullMapZoom's figures come from, faint at 0.15 s and nearly whole at 0.6 s.
constexpr double kFadeSeconds = 0.65;

// FullMapZoom's figures, measured off two 60 fps recordings, November 2026, four openings and four
// closings, with scripts/measure_map_zoom.py.
// - The scale's rate, both ways: within 0.6 of this every time, the line missing no frame from 13
//   times down by more than 0.4.
// - When the opening lands, after the Present that sees IsMapOpen, so the scale it starts from,
//   which the closing goes no further than; and when the closing starts, after the Present that
//   sees the world back. Both carry how far the game's picture comes after our Present: our own
//   markers, drawn without it, ran 38-39 ms ahead opening and 42-43 ms closing. It may be a frame
//   or two of the game's pipeline rather than a time, which another frame rate would move.
// - The closing's fade, roughly: the map's edges against the settled map's, frame by frame, were
//   down to the world's noise by about this.
constexpr double kZoomPerSecond = 39.5;
constexpr double kZoomOpenSeconds = 0.54;
constexpr double kZoomMost = 1.0 + kZoomPerSecond * kZoomOpenSeconds;
constexpr double kZoomCloseDelay = 0.04;
constexpr double kCloseFadeSeconds = 0.4;

// StandardMarker.MiniMap: an icon this far above or below the player in metres says so, while
// the map is zoomed in further than this.
constexpr float kAboveBelowMetres = 30.0f;
constexpr double kAboveBelowMostScale = 2.0;
// The size of the above and below images, which Pathing draws at their own pixel size, in Blish
// HUD's pixels at MapScale 1.
constexpr float kAboveBelowSize = 16.0f;

// StandardTrail.MiniMap.DrawSegment: a stretch of trail fades by half a percent per metre
// between its height and the player's, to no less than this.
constexpr float kTrailFadePerMetre = 0.005f;
constexpr float kTrailFadeLeast = 0.15f;

// WorldUtil.WorldToGameCoord's inches to the metre, for how far away a marker is.
constexpr float kInchesPerMetre = 39.37f;

// Under one step of an 8-bit alpha.
constexpr float kInvisibleOpacity = 0.003f;

bool allowed(Visibility level, bool attribute) {
    if (level == Visibility::Always) return true;
    if (level == Visibility::Never) return false;
    return attribute;
}

// Blish HUD's Z-up position of a point in MumbleLink's space.
glm::vec3 toBlish(const glm::vec3& position) {
    return glm::vec3(position.x, position.z, position.y);
}

// A BakedMarker's position as Pathing's Position has it: without heightOffset, Blish HUD's axes.
glm::vec3 markerPosition(const BakedMarker& marker) {
    return toBlish(marker.position - glm::vec3(0.0f, marker.height_offset, 0.0f));
}

// What Pathing scales an icon down by: MapScale in its own pixels, or 1 for an icon that keeps
// its size whatever the zoom.
double zoomScale(const map::View& view, bool scale_with_zoom) {
    return scale_with_zoom ? view.map_scale * kBlishPixel : 1.0;
}

// Cuts the segment a→b to the box, Liang–Barsky. False when none of it is inside.
bool clipSegment(glm::vec2* a, glm::vec2* b, const glm::vec2& min, const glm::vec2& max) {
    const glm::vec2 d = *b - *a;
    float enter = 0.0f;
    float leave = 1.0f;
    const float p[4] = {-d.x, d.x, -d.y, d.y};
    const float q[4] = {a->x - min.x, max.x - a->x, a->y - min.y, max.y - a->y};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) return false;
            continue;
        }
        const float t = q[i] / p[i];
        if (p[i] < 0.0f) {
            enter = std::max(enter, t);
        } else {
            leave = std::min(leave, t);
        }
        if (enter > leave) return false;
    }
    const glm::vec2 start = *a;
    *a = start + d * enter;
    *b = start + d * leave;
    return true;
}

// Whether every corner of the continent box lands on one side outside the bounds:
// StandardTrail.MiniMap.IsOutside.
bool outside(const map::View& view, const glm::dvec2& min, const glm::dvec2& max) {
    const glm::vec2 corners[4] = {
        map::project(view, min),
        map::project(view, glm::dvec2(max.x, min.y)),
        map::project(view, glm::dvec2(min.x, max.y)),
        map::project(view, max),
    };
    const auto all = [&corners](auto test) { return std::all_of(std::begin(corners), std::end(corners), test); };
    return all([&](const glm::vec2& c) { return c.x < view.min.x; }) || all([&](const glm::vec2& c) { return c.x >= view.max.x; }) ||
           all([&](const glm::vec2& c) { return c.y < view.min.y; }) || all([&](const glm::vec2& c) { return c.y >= view.max.y; });
}

// Draws the image over [min, max], cut to the view's bounds, its uvs cut with it.
void drawClippedImage(ImDrawList* list, ImTextureID texture, glm::vec2 min, glm::vec2 max, ImU32 colour, const map::View& view) {
    const glm::vec2 size = max - min;
    if (size.x <= 0.0f || size.y <= 0.0f) return;
    const glm::vec2 cut_min = glm::max(min, view.min);
    const glm::vec2 cut_max = glm::min(max, view.max);
    if (cut_min.x >= cut_max.x || cut_min.y >= cut_max.y) return;
    const glm::vec2 uv_min = (cut_min - min) / size;
    const glm::vec2 uv_max = (cut_max - min) / size;
    list->AddImage(
        texture,
        ImVec2(cut_min.x, cut_min.y),
        ImVec2(cut_max.x, cut_max.y),
        ImVec2(uv_min.x, uv_min.y),
        ImVec2(uv_max.x, uv_max.y),
        colour);
}

// "##,###": thousands grouped, no decimals, and nothing at all for zero.
std::string groupedInteger(double value) {
    const long long whole = std::llround(value);
    if (whole == 0) return "";
    std::string digits = std::to_string(whole < 0 ? -whole : whole);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(static_cast<size_t>(i), ",");
    return whole < 0 ? "-" + digits : digits;
}

bool isBlank(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); });
}

// The labels from the root category down to `node`. `node` must be in the tree.
std::vector<std::string> categoryChain(const registry::CategoryTree& tree, size_t node) {
    std::vector<std::string> chain;
    for (; node != pack::kNoIndex; node = tree.nodes[node].parent) chain.push_back(tree.nodes[node].label);
    std::reverse(chain.begin(), chain.end());
    return chain;
}

// A category label cut short for the tooltip's path, "Haskha's..." for "Haskha's Markers": at
// a space when one leaves at least half of what is kept, else mid-word. Counted in code points,
// so a cut never splits a UTF-8 character.
std::string shortLabel(const std::string& label) {
    // A typical name stays whole while a deep path still fits on a line or two.
    constexpr size_t kMaxChars = 12;
    constexpr std::string_view kEllipsis = "...";
    constexpr size_t kKeptChars = kMaxChars - kEllipsis.size();

    std::vector<size_t> starts; // the byte each code point starts at
    for (size_t i = 0; i < label.size(); ++i) {
        if ((static_cast<unsigned char>(label[i]) & 0xC0) != 0x80) starts.push_back(i);
    }
    if (starts.size() <= kMaxChars) return label;

    size_t cut = starts[kKeptChars];
    for (size_t n = kKeptChars; n > kKeptChars / 2; --n) {
        if (label[starts[n]] == ' ') {
            cut = starts[n];
            break;
        }
    }
    std::string kept = label.substr(0, cut);
    while (!kept.empty() && kept.back() == ' ') kept.pop_back();
    return kept.append(kEllipsis);
}

} // namespace

std::shared_ptr<const TrailLines> TrailCache::get(const runtime::LiveMap& live, const map::MapDetails& details) {
    // Trails come from the packs alone, so the map and the packs it was built from say
    // whether they are the same ones; a script can remove one, which a frame skips.
    if (lines_ && live.map_id == map_id_ && live.revision == revision_ && live.trails.size() == trail_count_ && &details == details_) {
        return lines_;
    }
    auto lines = std::make_shared<TrailLines>();
    lines->paths.reserve(live.trails.size());
    for (const runtime::LiveTrail& live_trail : live.trails) {
        const BakedTrail& trail = live_trail.trail;
        Path path;
        path.first = lines->lines.size();
        path.colour = trail.sample_colour; // what Pathing draws a trail with on the map: never its alpha
        path.min = glm::dvec2(std::numeric_limits<double>::max());
        path.max = glm::dvec2(std::numeric_limits<double>::lowest());
        if (trail.geometry) {
            for (const pack::trl::Segment& segment : trail.geometry->segments) {
                for (size_t i = 0; i + 1 < segment.points.size(); ++i) {
                    const glm::vec3& from = segment.points[i];
                    const glm::vec3& to = segment.points[i + 1];
                    // MumbleLink's y is up.
                    const Line line{map::continentPosition(details, from), map::continentPosition(details, to), (from.y + to.y) * 0.5f};
                    path.min = glm::min(path.min, glm::min(line.from, line.to));
                    path.max = glm::max(path.max, glm::max(line.from, line.to));
                    lines->lines.push_back(line);
                }
            }
        }
        path.count = lines->lines.size() - path.first;
        lines->paths.push_back(path);
    }
    map_id_ = live.map_id;
    revision_ = live.revision;
    trail_count_ = live.trails.size();
    details_ = &details;
    lines_ = std::move(lines);
    return lines_;
}

float FadeIn::update(const MumbleState& mumble, double seconds) {
    if (mumble.is_map_open != map_open_ || mumble.ui_size != ui_size_) {
        map_open_ = mumble.is_map_open;
        ui_size_ = mumble.ui_size;
        since_ = seconds;
    }
    return static_cast<float>(std::clamp((seconds - since_) / kFadeSeconds, 0.0, 1.0));
}

FullMapZoom::Frame FullMapZoom::update(bool map_open, bool world_drawn, double seconds) {
    if (!map_open) {
        *this = FullMapZoom{};
        return Frame{};
    }
    if (!map_open_) {
        *this = FullMapZoom{};
        map_open_ = true;
        opened_at_ = seconds;
    }

    // Frames in a row without the world, once the opening is done, that say the map is fully up;
    // more than one, so a frame GoodRender happened to miss is not taken for it.
    constexpr int kFramesWithoutWorld = 3;
    if (!closing_at_ && seconds - opened_at_ >= kZoomOpenSeconds) {
        if (!world_drawn) {
            ++frames_without_world_;
        } else if (frames_without_world_ >= kFramesWithoutWorld) {
            closing_at_ = seconds;
        } else {
            frames_without_world_ = 0;
        }
    }

    Frame frame;
    if (closing_at_) {
        const double closing = std::max(0.0, seconds - *closing_at_ - kZoomCloseDelay);
        frame.zoom = static_cast<float>(std::min(kZoomMost, 1.0 + kZoomPerSecond * closing));
        frame.opacity = static_cast<float>(std::clamp(1.0 - closing / kCloseFadeSeconds, 0.0, 1.0));
    } else {
        frame.zoom = static_cast<float>(1.0 + kZoomPerSecond * std::max(0.0, kZoomOpenSeconds - (seconds - opened_at_)));
    }
    return frame;
}

Compass compassOf(const MumbleState& mumble) {
    Compass compass;
    compass.width = mumble.compass_width;
    compass.height = mumble.compass_height;
    compass.rotation = mumble.compass_rotation;
    compass.rotating = mumble.is_compass_rotation_enabled;
    compass.top_right = mumble.is_compass_top_right;
    compass.scale = mumble.map_scale;
    compass.centre = mumble.map_center;
    return compass;
}

MumbleState withCompass(MumbleState mumble, const Compass& compass) {
    mumble.compass_width = compass.width;
    mumble.compass_height = compass.height;
    mumble.compass_rotation = compass.rotation;
    mumble.is_compass_rotation_enabled = compass.rotating;
    mumble.is_compass_top_right = compass.top_right;
    mumble.map_scale = compass.scale;
    mumble.map_center = compass.centre;
    mumble.is_map_open = false;
    return mumble;
}

CompassKeeper::Kept CompassKeeper::update(const MumbleState& mumble) {
    const Compass now = compassOf(mumble);
    if (mumble.is_map_open) {
        full_map_ = now;
        keep_ = true;
    } else if (!keep_ || now.scale != full_map_.scale || now.centre != full_map_.centre) {
        // Exactly the full map's last scale and centre is its fields not yet given back; the
        // compass's own being those to the bit does not happen.
        keep_ = false;
        compass_ = now;
    }
    return Kept{compass_, keep_};
}

void build(
    const std::vector<Layer>& layers,
    const map::MapDetails& details,
    const MumbleState& mumble,
    const Options& options,
    float opacity,
    float ui_scale,
    MapScene* out) {
    out->full_map = mumble.is_map_open;
    out->ui_scale = ui_scale;
    out->compass = compassOf(mumble);
    out->keep_compass = false;
    out->zoom = 1.0f;
    out->trail_width = options.trail_width;
    out->fade_far_trails = options.fade_far_trails;
    out->opacity = 0.0f;
    out->trails.clear();
    out->paths.clear();
    out->icons.clear();

    if (!options.enabled) return;
    // Pathing's ignore.yaml, Compass and Map alike: the Super Adventure Box's lobby and its two
    // worlds, which have no minimap or map of the usual kind to draw on.
    constexpr int kNoMapDrawn[] = {935, 895, 934};
    if (std::find(std::begin(kNoMapDrawn), std::end(kNoMapDrawn), mumble.map_id) != std::end(kNoMapDrawn)) return;
    // FlatMap's override opacity, under the fade.
    out->opacity = opacity * (out->full_map ? options.map_opacity : options.minimap_opacity);
    if (out->opacity <= kInvisibleOpacity) return;

    const Visibility marker_level = out->full_map ? options.map_markers : options.minimap_markers;
    const Visibility trail_level = out->full_map ? options.map_trails : options.minimap_trails;
    const float player_height = mumble.player_position.z;
    const glm::vec3 camera = mumble.camera_position;

    for (size_t layer = 0; layer < layers.size(); ++layer) {
        const runtime::LiveMap& live = *layers[layer].live;
        const std::vector<char>& visible = *layers[layer].visible;
        const TextureLookup& texture = layers[layer].texture;

        const std::shared_ptr<const TrailLines> lines = layers[layer].trails->get(live, details);
        out->trails.push_back(lines);
        for (size_t i = 0; i < live.trails.size(); ++i) {
            const runtime::LiveTrail& live_trail = live.trails[i];
            const BakedTrail& trail = live_trail.trail;
            if (live_trail.removed || lines->paths[i].count == 0 || !runtime::nodeVisible(live_trail.node, visible)) continue;
            if (live_trail.behavior.filtered && !options.show_hidden) continue;
            if (!allowed(trail_level, out->full_map ? trail.map_visibility : trail.minimap_visibility)) continue;
            Path path = lines->paths[i];
            path.layer = layer;
            out->paths.push_back(path);
        }

        for (size_t i = 0; i < live.markers.size(); ++i) {
            const runtime::LiveMarker& live_marker = live.markers[i];
            const BakedMarker& marker = live_marker.marker;
            if (live_marker.removed || !runtime::nodeVisible(live_marker.node, visible)) continue;
            if (live_marker.behavior.filtered && !options.show_hidden) continue;
            if (!allowed(marker_level, out->full_map ? marker.map_visibility : marker.minimap_visibility)) continue;
            const ImTextureID image = keptImage(texture, layers[layer].edited, live_marker.texture_pack, marker.icon, &live_marker.image);
            if (!image) continue;

            const glm::vec3 position = markerPosition(marker);
            Icon icon;
            icon.layer = layer;
            icon.marker = i;
            icon.texture = image;
            icon.colour = applyOpacity(marker.tint, marker.alpha * out->opacity);
            icon.continent = map::continentPosition(details, marker.position);
            icon.size = marker.map_display_size;
            icon.scale_with_zoom = marker.scale_on_map_with_zoom;
            if (options.above_below) {
                const float rise = position.z - player_height;
                if (std::abs(rise) > kAboveBelowMetres) icon.above_below = rise > 0.0f ? 1 : -1;
            }
            const glm::vec3 d = position - camera;
            icon.depth = glm::dot(d, d);
            out->icons.push_back(icon);
        }
    }
    // Farthest first, so the nearest lands on top, the packs' order kept between equals. Gathered into a list kept
    // between frames, and swapped in, so neither allocates once grown.
    static std::vector<DepthKey> keys;
    static std::vector<Icon> sorted;
    backToFront(out->icons, &keys);
    sorted.clear();
    for (const DepthKey& key : keys) sorted.push_back(out->icons[key.index]);
    out->icons.swap(sorted);
}

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
    MapScene* out) {
    build({Layer{&live, &visible, texture, trails}}, details, mumble, options, opacity, ui_scale, out);
}

bool iconRect(const Icon& icon, const map::View& view, glm::vec2* min, glm::vec2* max) {
    const glm::vec2 location = map::project(view, icon.continent);
    if (!view.contains(location)) return false;
    // mapDisplaySize Blish HUD pixels, shrunk by the zoom, in screen pixels.
    const float size = static_cast<float>(icon.size * view.ui_scale * kBlishPixel / zoomScale(view, icon.scale_with_zoom));
    *min = location - glm::vec2(size * 0.5f);
    *max = location + glm::vec2(size * 0.5f);
    return true;
}

float lineOpacity(const MapScene& scene, const Line& line, float player_height) {
    if (!scene.fade_far_trails) return scene.opacity;
    return scene.opacity * std::clamp(1.0f - std::abs(line.height - player_height) * kTrailFadePerMetre, kTrailFadeLeast, 1.0f);
}

void draw(ImDrawList* list, const MapScene& scene, const map::View& view, float player_height) {
    const float blish_pixel = view.ui_scale * kBlishPixel;
    const int first_vertex = list->VtxBuffer.Size;

    // A stretch ending within this many pixels of where the last line drawn ends is joined onto the next, which is
    // drawn from there: zoomed out, a map's hundreds of thousands of points land on a few thousand pixels, and a line a
    // fraction of a pixel long costs as much as any. What is drawn strays from the trail by under this.
    constexpr float kLeastPixels = 1.0f;

    // In whole pixels: ImGui draws a line of whole pixels from the atlas's baked lines, anti-aliased by the texture,
    // and any other as more triangles for its edges, twice the vertices (ImDrawList::AddPolyline).
    const float width = std::max(1.0f, std::round(scene.trail_width * blish_pixel));
    for (const Path& path : scene.paths) {
        if (outside(view, path.min, path.max)) continue;
        const std::vector<Line>& lines = scene.trails[path.layer]->lines;
        const size_t end = path.first + path.count;
        glm::vec2 start(0.0f); // where the next line drawn starts
        for (size_t i = path.first; i < end; ++i) {
            const Line& line = lines[i];
            // A segment of the trail starts where the one before it did not end, and is never joined to it.
            if (i == path.first || line.from != lines[i - 1].to) start = map::project(view, line.from);
            const glm::vec2 reached = map::project(view, line.to);
            const bool segment_ends = i + 1 == end || lines[i + 1].from != line.to;
            if (segment_ends || glm::distance(start, reached) >= kLeastPixels) {
                glm::vec2 from = start;
                glm::vec2 to = reached;
                if (clipSegment(&from, &to, view.min, view.max)) {
                    list->AddLine(
                        ImVec2(from.x, from.y),
                        ImVec2(to.x, to.y),
                        applyOpacity(path.colour, lineOpacity(scene, line, player_height)),
                        width);
                }
                start = reached;
            }
        }
    }

    for (const Icon& icon : scene.icons) {
        glm::vec2 min, max;
        if (!iconRect(icon, view, &min, &max)) continue;
        drawClippedImage(list, icon.texture, min, max, icon.colour, view);

        const double zoom = zoomScale(view, icon.scale_with_zoom);
        const ImTextureID indicator = icon.above_below > 0 ? scene.above : scene.below;
        if (icon.above_below == 0 || !indicator || zoom >= kAboveBelowMostScale) continue;
        // In the icon's top right corner, at the icon's own scale; as opaque as the map, whatever
        // the marker's alpha or tint, as Pathing draws it.
        const float size = static_cast<float>(kAboveBelowSize * blish_pixel / zoom);
        const glm::vec2 corner(max.x - size, min.y);
        drawClippedImage(list, indicator, corner, corner + glm::vec2(size), applyOpacity(IM_COL32_WHITE, scene.opacity), view);
    }

    // The whole picture scaled, as the game scales its map's, icons and all: what is outside the
    // bounds at the view MumbleLink describes is outside them zoomed too.
    if (scene.zoom != 1.0f) {
        const ImVec2 middle((view.min.x + view.max.x) * 0.5f, (view.min.y + view.max.y) * 0.5f);
        for (int i = first_vertex; i < list->VtxBuffer.Size; ++i) {
            ImVec2& pos = list->VtxBuffer[i].pos;
            pos = ImVec2(middle.x + (pos.x - middle.x) * scene.zoom, middle.y + (pos.y - middle.y) * scene.zoom);
        }
    }
}

const Icon* hovered(const MapScene& scene, const map::View& view, glm::vec2 mouse) {
    if (!view.contains(mouse)) return nullptr;
    for (auto it = scene.icons.rbegin(); it != scene.icons.rend(); ++it) {
        glm::vec2 min, max;
        if (iconRect(*it, view, &min, &max) && mouse.x >= min.x && mouse.y >= min.y && mouse.x < max.x && mouse.y < max.y) {
            return &*it;
        }
    }
    return nullptr;
}

Tooltip tooltip(const runtime::LiveMarker& marker, const registry::CategoryTree& tree, const glm::vec3& player, bool category_path) {
    Tooltip tip;
    if (category_path) {
        if (marker.node >= tree.nodes.size()) {
            tip.title = "Unassigned Category";
            return tip;
        }
        for (const std::string& label : categoryChain(tree, marker.node)) {
            if (!tip.title.empty()) tip.title += "\n > ";
            tip.title += label;
        }
        return tip;
    }

    tip.title = marker.marker.tip_name;
    if (tip.title.empty()) return tip;
    if (!isBlank(marker.marker.tip_description)) tip.description = marker.marker.tip_description + "\n\n";

    // Where the marker sits, each category cut short. The title is usually the marker's own
    // category's name, which is not said twice.
    if (marker.node < tree.nodes.size()) {
        std::vector<std::string> chain = categoryChain(tree, marker.node);
        if (chain.back() == tip.title) chain.pop_back();
        std::string path;
        for (const std::string& label : chain) {
            if (!path.empty()) path += "/";
            path += shortLabel(label);
        }
        if (!path.empty()) tip.description += path + "\n";
    }

    const glm::vec3 position = markerPosition(marker.marker);
    tip.description += groupedInteger(glm::distance(position, player) * kInchesPerMetre) + " away";
    // How far up or down, in the same units; nothing when level, as groupedInteger says.
    const float rise = position.z - player.z;
    const std::string height = groupedInteger(std::abs(rise) * kInchesPerMetre);
    if (!height.empty()) tip.description += ", " + height + (rise > 0.0f ? " above" : " below");
    return tip;
}

glm::vec2 tooltipPosition(glm::vec2 mouse, glm::vec2 size, glm::vec2 display) {
    // Between the cursor's tip and the tooltip, enough that they never touch.
    constexpr float kGap = 6.0f;

    glm::vec2 position(mouse.x + kGap, mouse.y - kGap - size.y);
    if (position.x + size.x > display.x || position.y < 0.0f) {
        position.x = std::max(0.0f, mouse.x - kGap - size.x);
        position.y = std::clamp(position.y, 0.0f, std::max(0.0f, display.y - size.y));
    }
    return position;
}

void publish(std::shared_ptr<const MapScene> minimap, std::shared_ptr<const MapScene> full_map) {
    latest_minimap.store(std::move(minimap));
    latest_full_map.store(std::move(full_map));
}

std::shared_ptr<const MapScene> published(bool full_map) {
    return full_map ? latest_full_map.load() : latest_minimap.load();
}

void drawPublished(ImDrawList* list, const Mumble::Data* mumble, glm::vec2 display, bool full_map) {
    const std::shared_ptr<const MapScene> scene = published(full_map);
    if (!scene) return;
    MumbleState state = contextState(mumble);
    if (full_map) {
        // The frame the map went down on draws nothing of it.
        if (!state.is_map_open) return;
    } else if (state.is_map_open || scene->keep_compass) {
        state = withCompass(std::move(state), scene->compass);
    }
    if (const std::optional<map::View> view = map::view(state, display, scene->ui_scale)) {
        draw(list, *scene, *view, state.player_position.z);
    }
}

} // namespace viewer::map_scene
