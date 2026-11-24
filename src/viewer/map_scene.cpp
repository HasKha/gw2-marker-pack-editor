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

std::atomic<std::shared_ptr<const MapScene>> latest;

// Blish HUD's pixels per UI pixel at the Normal interface size (GraphicsService.GetScaleRatio),
// which Pathing's sizes on the map are in: an icon's mapDisplaySize, a trail's width.
constexpr float kBlishPixel = 0.897f;

// FlatMap's fade-in, in seconds.
constexpr double kFadeSeconds = 0.65;

// StandardMarker.MiniMap: an icon this far above or below the player in metres says so, while
// the map is zoomed in further than this.
constexpr float kAboveBelowMetres = 30.0f;
constexpr double kAboveBelowMostScale = 2.0;
// The size of Pathing's above and below icons, in Blish HUD's pixels at MapScale 1.
constexpr float kAboveBelowSize = 16.0f;
// The colour of those icons, which point up or down.
constexpr ImU32 kAboveBelowColour = IM_COL32(93, 215, 216, 255);
constexpr ImU32 kAboveBelowOutline = IM_COL32(0, 0, 0, 200);

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

// Pathing's above or below icon, drawn as an arrow, where it fits inside the bounds whole.
void drawAboveBelow(ImDrawList* list, int direction, glm::vec2 min, glm::vec2 max, float opacity, const map::View& view) {
    if (min.x < view.min.x || min.y < view.min.y || max.x > view.max.x || max.y > view.max.y) return;
    const float middle = (min.x + max.x) * 0.5f;
    const ImVec2 tip(middle, direction > 0 ? min.y : max.y);
    const ImVec2 left(min.x, direction > 0 ? max.y : min.y);
    const ImVec2 right(max.x, direction > 0 ? max.y : min.y);
    list->AddTriangleFilled(tip, left, right, applyOpacity(kAboveBelowColour, opacity));
    list->AddTriangle(tip, left, right, applyOpacity(kAboveBelowOutline, opacity));
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
            const ImTextureID image = texture(live_marker.texture_pack, marker.icon);
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
    // Farthest first, so the nearest lands on top; a stable sort keeps the packs' order between equals.
    std::stable_sort(out->icons.begin(), out->icons.end(), [](const Icon& a, const Icon& b) { return a.depth > b.depth; });
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

    const float width = scene.trail_width * blish_pixel;
    for (const Path& path : scene.paths) {
        if (outside(view, path.min, path.max)) continue;
        for (size_t i = path.first; i < path.first + path.count; ++i) {
            const Line& line = scene.trails[path.layer]->lines[i];
            glm::vec2 from = map::project(view, line.from);
            glm::vec2 to = map::project(view, line.to);
            if (!clipSegment(&from, &to, view.min, view.max)) continue;
            list->AddLine(
                ImVec2(from.x, from.y),
                ImVec2(to.x, to.y),
                applyOpacity(path.colour, lineOpacity(scene, line, player_height)),
                width);
        }
    }

    for (const Icon& icon : scene.icons) {
        glm::vec2 min, max;
        if (!iconRect(icon, view, &min, &max)) continue;
        drawClippedImage(list, icon.texture, min, max, icon.colour, view);

        const double zoom = zoomScale(view, icon.scale_with_zoom);
        if (icon.above_below == 0 || zoom >= kAboveBelowMostScale) continue;
        // In the icon's top right corner, at the icon's own scale.
        const float size = static_cast<float>(kAboveBelowSize * blish_pixel / zoom);
        const float opacity = ((icon.colour >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        drawAboveBelow(list, icon.above_below, glm::vec2(max.x - size, min.y), glm::vec2(max.x, min.y + size), opacity, view);
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

    const float metres = glm::distance(markerPosition(marker.marker), player);
    tip.description += groupedInteger(metres * kInchesPerMetre) + " away";
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

void publish(std::shared_ptr<const MapScene> scene) {
    latest.store(std::move(scene));
}

std::shared_ptr<const MapScene> published() {
    return latest.load();
}

void drawPublished(ImDrawList* list, const Mumble::Data* mumble, glm::vec2 display) {
    const std::shared_ptr<const MapScene> scene = published();
    if (!scene) return;
    const MumbleState state = contextState(mumble);
    // The scene was decided for the other map: the frame it changed on draws nothing.
    if (state.is_map_open != scene->full_map) return;
    if (const std::optional<map::View> view = map::view(state, display, scene->ui_scale)) {
        draw(list, *scene, *view, state.player_position.z);
    }
}

} // namespace viewer::map_scene
