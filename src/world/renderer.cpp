#include "world/renderer.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "resources.h"

namespace world::renderer {

namespace {

Options draw_options;

// Blish HUD's StandardTrail.World.cs: TRAIL_WIDTH = 20 * 0.0254f, used as the half-width
// offset either side of the path, scaled by trailScale.
constexpr float kTrailHalfWidth = 20.0f * kMetresPerInch;

// Resolved once per pack revision so the per-frame loop never walks the category chain.
// The largest pack tested has 55,887 markers; doing inherited-attribute lookups for each
// of them every frame is not viable inside the render callback.
struct MarkerDraw {
    size_t pathable = pack::kNoIndex;
    glm::vec3 position{0.0f};
    int map_id = 0;
    float icon_size = 1.0f;
    float alpha = 1.0f;
    float min_size = 5.0f;
    float max_size = 2048.0f;
    float fade_near = -1.0f; // inches
    float fade_far = -1.0f; // inches
    ImU32 tint = IM_COL32_WHITE;
    std::string icon;
};

struct TrailDraw {
    size_t pathable = pack::kNoIndex;
    int map_id = 0;
    float alpha = 1.0f;
    float scale = 1.0f;
    bool is_wall = false;
    float fade_near = 700.0f;
    float fade_far = 900.0f;
    ImU32 tint = IM_COL32_WHITE;
    std::string texture;
    std::string data;
};

struct Cache {
    uint64_t revision = 0;
    const void* pack = nullptr;
    std::vector<MarkerDraw> markers;
    std::vector<TrailDraw> trails;
} cache;

ImU32 tintOf(const pack::Pack& pack, const pack::Pathable& pathable) {
    uint32_t argb = 0xFFFFFFFFu;
    // `tint` and `color` are aliases; whichever the author used wins.
    const std::string* text = pack.effectiveValue(pathable, "color");
    if (!text) text = pack.effectiveValue(pathable, "tint");
    if (text) pack::parseColor(*text, &argb);

    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

std::string stringOf(const pack::Pack& pack, const pack::Pathable& pathable, const char* key) {
    const std::string* value = pack.effectiveValue(pathable, key);
    return value ? *value : std::string();
}

void rebuild(editor::State& state) {
    cache.markers.clear();
    cache.trails.clear();
    cache.revision = state.revision;
    cache.pack = &state.pack;

    for (size_t i = 0; i < state.pack.pathables.size(); ++i) {
        const pack::Pathable& pathable = state.pack.pathables[i];
        if (pathable.type == pack::PathableType::Route) continue; // deprecated, never drawn

        const int map_id = state.pack.effectiveInt(pathable, "mapid");

        if (pathable.type == pack::PathableType::Poi) {
            // A POI without a position is an invisible anchor; the loader drops those and
            // so do we. See docs/format.md.
            const std::string* x = pathable.attributes.value("xpos");
            const std::string* y = pathable.attributes.value("ypos");
            const std::string* z = pathable.attributes.value("zpos");
            if (!x || !y || !z) continue;

            MarkerDraw marker;
            if (!pack::parseFloat(*x, &marker.position.x)) continue;
            if (!pack::parseFloat(*y, &marker.position.y)) continue;
            if (!pack::parseFloat(*z, &marker.position.z)) continue;

            marker.pathable = i;
            marker.map_id = map_id;
            marker.position.y += state.pack.effectiveFloat(pathable, "heightoffset");
            marker.icon_size = state.pack.effectiveFloat(pathable, "iconsize");
            marker.alpha = state.pack.effectiveFloat(pathable, "alpha");
            marker.min_size = state.pack.effectiveFloat(pathable, "minsize");
            marker.max_size = state.pack.effectiveFloat(pathable, "maxsize");
            marker.fade_near = state.pack.effectiveFloat(pathable, "fadenear");
            marker.fade_far = state.pack.effectiveFloat(pathable, "fadefar");
            marker.tint = tintOf(state.pack, pathable);
            marker.icon = stringOf(state.pack, pathable, "iconfile");

            cache.markers.push_back(std::move(marker));
            continue;
        }

        TrailDraw trail;
        trail.pathable = i;
        trail.map_id = map_id;
        trail.alpha = state.pack.effectiveFloat(pathable, "alpha");
        trail.scale = state.pack.effectiveFloat(pathable, "trailscale");
        trail.is_wall = state.pack.effectiveBool(pathable, "iswall");
        trail.tint = tintOf(state.pack, pathable);
        trail.texture = stringOf(state.pack, pathable, "texture");
        trail.data = stringOf(state.pack, pathable, "traildata");

        // Trails default to 700/900 where markers default to disabled, so the table's
        // marker defaults cannot be used here. See docs/format.md.
        const std::string* near_text = state.pack.effectiveValue(pathable, "fadenear");
        const std::string* far_text = state.pack.effectiveValue(pathable, "fadefar");
        if (!near_text || !pack::parseFloat(*near_text, &trail.fade_near)) trail.fade_near = 700.0f;
        if (!far_text || !pack::parseFloat(*far_text, &trail.fade_far)) trail.fade_far = 900.0f;

        cache.trails.push_back(std::move(trail));
    }
}

// Blish fades linearly between fadeNear and fadeFar, both in inches from the player.
float fadeFactor(float distance_inches, float fade_near, float fade_far) {
    if (fade_near < 0.0f || fade_far <= fade_near) return 1.0f;
    if (distance_inches >= fade_far) return 0.0f;
    if (distance_inches <= fade_near) return 1.0f;
    return 1.0f - (distance_inches - fade_near) / (fade_far - fade_near);
}

ImU32 applyOpacity(ImU32 tint, float opacity) {
    const float alpha = ((tint >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f * std::clamp(opacity, 0.0f, 1.0f);
    return (tint & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha * 255.0f + 0.5f) << IM_COL32_A_SHIFT);
}

struct Sorted {
    float distance = 0.0f;
    size_t index = 0;
    bool is_trail = false;
};

void drawMarkers(editor::State& state, const Camera& camera, ImDrawList* list, std::vector<Sorted>& order) {
    for (const Sorted& entry : order) {
        if (entry.is_trail) continue;

        const MarkerDraw& marker = cache.markers[entry.index];

        glm::vec2 screen;
        if (!camera.project(marker.position, &screen)) continue;

        const float opacity = marker.alpha * fadeFactor(metresToInches(entry.distance), marker.fade_near, marker.fade_far);
        if (opacity <= 0.003f) continue;

        const float camera_distance = glm::length(marker.position - camera.position);
        // Blish scales the unit quad by iconSize * 2, then clamps the on-screen height
        // between minSize*4 and maxSize*4 pixels.
        float pixels = camera.pixelSize(marker.icon_size * 2.0f, camera_distance);
        pixels = std::clamp(pixels, marker.min_size * 4.0f, marker.max_size * 4.0f);
        if (pixels < 1.0f) continue;

        const float half = pixels * 0.5f;
        const ImVec2 min(screen.x - half, screen.y - half);
        const ImVec2 max(screen.x + half, screen.y + half);

        const bool selected =
            draw_options.highlight_selection && state.selection.contains(editor::NodeRef{editor::NodeKind::Pathable, marker.pathable});

        if (ImTextureID texture = resources::texture(marker.icon)) {
            list->AddImage(texture, min, max, ImVec2(0, 0), ImVec2(1, 1), applyOpacity(marker.tint, opacity));
        } else {
            // No icon, or it failed to load: a dot still shows the marker is there.
            list->AddCircleFilled(ImVec2(screen.x, screen.y), std::max(2.0f, half * 0.35f), applyOpacity(marker.tint, opacity * 0.7f));
        }

        if (selected) {
            list->AddRect(
                ImVec2(min.x - 2.0f, min.y - 2.0f),
                ImVec2(max.x + 2.0f, max.y + 2.0f),
                IM_COL32(255, 210, 60, 255),
                0.0f,
                0,
                2.0f);
        }
    }
}

// One ribbon quad per segment, built in world space and projected corner by corner so the
// width is perspective-correct. Mirrors how Blish builds its vertex buffer, minus the
// resolution resampling.
void drawTrailSection(
    const Camera& camera,
    ImDrawList* list,
    const pack::trl::Segment& segment,
    const TrailDraw& trail,
    ImTextureID texture) {
    if (segment.points.size() < 2) return;

    const float half_width = kTrailHalfWidth * trail.scale;
    const glm::vec3 down(0.0f, -1.0f, 0.0f);

    // Texture repeats once per (half_width * 2) of path length, matching Blish's UVs.
    const float repeat = std::max(kTrailHalfWidth * 2.0f, 0.0001f);
    float travelled = 0.0f;

    for (size_t i = 0; i + 1 < segment.points.size(); ++i) {
        const glm::vec3& a = segment.points[i];
        const glm::vec3& b = segment.points[i + 1];

        const glm::vec3 direction = b - a;
        const float length = glm::length(direction);
        if (length < 0.0001f) continue;

        // Flat: perpendicular to the path in the horizontal plane.
        // Wall:  perpendicular to the path and vertical.
        glm::vec3 offset = trail.is_wall ? glm::cross(direction, glm::cross(direction, down)) : glm::cross(direction, down);
        const float offset_length = glm::length(offset);
        if (offset_length < 0.0001f) {
            travelled += length;
            continue;
        }
        offset = offset / offset_length * half_width;

        glm::vec2 p0, p1, p2, p3;
        if (!camera.project(a - offset, &p0)) {
            travelled += length;
            continue;
        }
        if (!camera.project(a + offset, &p1)) {
            travelled += length;
            continue;
        }
        if (!camera.project(b + offset, &p2)) {
            travelled += length;
            continue;
        }
        if (!camera.project(b - offset, &p3)) {
            travelled += length;
            continue;
        }

        // Fade is measured from the player, not the camera — same as markers, and same as
        // Blish's DistanceToPlayer.
        const float distance = glm::length((a + b) * 0.5f - camera.player);
        const float opacity = trail.alpha * fadeFactor(metresToInches(distance), trail.fade_near, trail.fade_far);
        if (opacity <= 0.003f) {
            travelled += length;
            continue;
        }

        const ImU32 colour = applyOpacity(trail.tint, opacity);
        const float v0 = travelled / repeat;
        const float v1 = (travelled + length) / repeat;

        if (texture) {
            list->AddImageQuad(
                texture,
                ImVec2(p0.x, p0.y),
                ImVec2(p1.x, p1.y),
                ImVec2(p2.x, p2.y),
                ImVec2(p3.x, p3.y),
                ImVec2(1.0f, v0),
                ImVec2(0.0f, v0),
                ImVec2(0.0f, v1),
                ImVec2(1.0f, v1),
                colour);
        } else {
            const ImVec2 quad[4] = {ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), ImVec2(p3.x, p3.y)};
            list->AddConvexPolyFilled(quad, 4, colour);
        }

        travelled += length;
    }
}

void drawTrails(editor::State& state, const Camera& camera, ImDrawList* list, std::vector<Sorted>& order) {
    for (const Sorted& entry : order) {
        if (!entry.is_trail) continue;

        const TrailDraw& trail = cache.trails[entry.index];
        const pack::trl::Trail* data = resources::trail(trail.data);
        if (!data) continue;

        ImTextureID texture = resources::texture(trail.texture);

        const bool selected =
            draw_options.highlight_selection && state.selection.contains(editor::NodeRef{editor::NodeKind::Pathable, trail.pathable});

        TrailDraw effective = trail;
        if (selected) effective.tint = IM_COL32(255, 210, 60, (trail.tint >> IM_COL32_A_SHIFT) & 0xFF);

        for (const pack::trl::Segment& segment : data->segments) {
            drawTrailSection(camera, list, segment, effective, texture);
        }
    }
}

} // namespace

Options& options() {
    return draw_options;
}

void draw(editor::State& state, const Camera& camera) {
    if (!state.loaded || !camera.valid) return;

    if (cache.revision != state.revision || cache.pack != &state.pack) rebuild(state);

    // Behind our own windows but over the game.
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    if (!list) return;

    const int map_id = static_cast<int>(camera.map_id);

    std::vector<Sorted> order;
    order.reserve(256);

    if (draw_options.draw_markers) {
        for (size_t i = 0; i < cache.markers.size(); ++i) {
            const MarkerDraw& marker = cache.markers[i];
            if (!draw_options.ignore_map_filter && marker.map_id != map_id) continue;

            const float distance = glm::length(marker.position - camera.player);
            // Cheap reject before any projection work.
            if (marker.fade_far > 0.0f && metresToInches(distance) > marker.fade_far) continue;

            order.push_back(Sorted{distance, i, false});
        }
    }

    if (draw_options.draw_trails) {
        for (size_t i = 0; i < cache.trails.size(); ++i) {
            if (!draw_options.ignore_map_filter && cache.trails[i].map_id != map_id) continue;
            // Trails span a large area, so a single distance is only a sorting hint.
            order.push_back(Sorted{0.0f, i, true});
        }
    }

    // Back to front: with no depth buffer, painter's order is all we have.
    std::sort(order.begin(), order.end(), [](const Sorted& a, const Sorted& b) { return a.distance > b.distance; });

    drawTrails(state, camera, list, order);
    drawMarkers(state, camera, list, order);
}

} // namespace world::renderer
