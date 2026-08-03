#include "world/renderer.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "resources.h"
#include "trail_edit.h"

namespace world::renderer {

namespace {

// Blish HUD's StandardTrail.World.cs: TRAIL_WIDTH = 20 * 0.0254f, used as the half-width
// offset either side of the path, scaled by trailScale.
constexpr float kTrailHalfWidth = 20.0f * kMetresPerInch;

// What minSize and maxSize multiply up to, in real pixels.
//
// StandardMarker.World.cs clamps between minSize*4 and maxSize*4, but the quantity it
// clamps is not a pixel height:
//
//     pixelSizeY = bounds.HalfExtents.Y * 2 * Viewport.Height
//
// `bounds` is in normalized device coordinates, which span **2** across the viewport, so
// multiplying an NDC extent by the viewport height without halving it yields twice the
// pixel height. Its floor of minSize*4 therefore bites at minSize*2 real pixels.
//
// Matching that is the point: this editor exists to show what Blish will show. With the
// literal *4 a marker pins to a fixed size twice as far out and twice as large, which is
// the "icons never get smaller in the distance" bug this replaced.
constexpr float kBlishSizeLimit = 2.0f;

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

    // Any of rotate / rotate-x / rotate-y / rotate-z turns the marker from a billboard
    // into a fixed-orientation quad — including rotate="0,0,0", which is not a no-op.
    bool has_rotation = false;
    glm::vec3 rotation{0.0f}; // radians
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

// Frontmost marker under the mouse, recomputed each draw(). Atomic because the WndProc
// callback reads it from the game's message thread.
std::atomic<size_t> hovered_pathable{pack::kNoIndex};

// What to outline as hovered this frame. The live value is only complete once every
// marker has been tested, so the highlight uses the previous frame's result — a frame of
// latency nobody can see, and it makes picking visible instead of a guess.
size_t hover_highlight = pack::kNoIndex;

// The same, for the points of the trail open in the editor. They are drawn as handles
// because a coordinate has nothing to click otherwise.
std::atomic<size_t> hovered_trail_point{pack::kNoIndex};
size_t hover_point_highlight = pack::kNoIndex;

bool pointInQuad(const ImVec2& p, const ImVec2 quad[4]) {
    // Sign of the cross product against each edge; consistent sign means inside. Works
    // for any convex quad, which a projected marker always is.
    bool positive = false;
    bool negative = false;
    for (int i = 0; i < 4; ++i) {
        const ImVec2& a = quad[i];
        const ImVec2& b = quad[(i + 1) % 4];
        const float cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (cross > 0.0f) positive = true;
        if (cross < 0.0f) negative = true;
        if (positive && negative) return false;
    }
    return true;
}

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

constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

// rotate="x,y,z" with any number of components; Blish fills the rest with zero.
void applyRotateList(std::string_view text, glm::vec3* out) {
    size_t component = 0;
    size_t begin = 0;
    while (component < 3) {
        const size_t comma = text.find(',', begin);
        const std::string_view part = text.substr(begin, comma == std::string_view::npos ? std::string_view::npos : comma - begin);

        float degrees = 0.0f;
        if (pack::parseFloat(part, &degrees)) (*out)[static_cast<glm::length_t>(component)] = degrees * kDegreesToRadians;

        ++component;
        if (comma == std::string_view::npos) break;
        begin = comma + 1;
    }
}

// Mirrors Pathing's Populate_Rotation: degrees in, radians out, and the presence of *any*
// rotate attribute is what disables billboarding.
bool resolveRotation(const pack::Pack& pack, const pack::Pathable& pathable, glm::vec3* out) {
    bool present = false;
    *out = glm::vec3(0.0f);

    if (const std::string* text = pack.effectiveValue(pathable, "rotate")) {
        applyRotateList(*text, out);
        present = true;
    }

    // The per-axis forms are applied after, and win, matching the reference order.
    static constexpr const char* kAxisKeys[3] = {"rotate-x", "rotate-y", "rotate-z"};
    for (int axis = 0; axis < 3; ++axis) {
        const std::string* text = pack.effectiveValue(pathable, kAxisKeys[axis]);
        if (!text) continue;
        float degrees = 0.0f;
        if (pack::parseFloat(*text, &degrees)) (*out)[axis] = degrees * kDegreesToRadians;
        present = true;
    }

    return present;
}

void rebuild(editor::State& state) {
    cache.markers.clear();
    cache.trails.clear();
    cache.revision = state.revision;
    cache.pack = &state.pack;

    for (size_t i = 0; i < state.pack.pathables.size(); ++i) {
        const pack::Pathable& pathable = state.pack.pathables[i];
        if (pathable.grave.deleted) continue;
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
            marker.has_rotation = resolveRotation(state.pack, pathable, &marker.rotation);

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

// Draws a marker that carries a rotate attribute: a quad fixed in the world rather than
// turned towards the camera.
//
// Blish builds this in its own Z-up space as
//   Scale(size*2) * RotX * RotY * RotZ * Translate(position)
// over a unit quad lying in the XY plane, i.e. flat on the ground facing up. Its space
// relates to the file's Y-up space by swapping Y and Z — the same swap TmfLib applies when
// reading .trl — so the offset is built in Blish's space and swapped back at the end.
// (Swapping two axes also flips handedness, which is exactly the LH-to-RH difference
// between Mumble's space and Blish's.)
bool rotatedMarkerCorners(const Camera& camera, const MarkerDraw& marker, ImVec2 out[4]) {
    const float half = marker.icon_size; // the quad is the unit square scaled by size*2
    const float cx = std::cos(marker.rotation.x), sx = std::sin(marker.rotation.x);
    const float cy = std::cos(marker.rotation.y), sy = std::sin(marker.rotation.y);
    const float cz = std::cos(marker.rotation.z), sz = std::sin(marker.rotation.z);

    static const glm::vec2 kCorners[4] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};

    for (int i = 0; i < 4; ++i) {
        glm::vec3 p(kCorners[i].x * half, kCorners[i].y * half, 0.0f);

        // X, then Y, then Z, matching the reference's matrix order.
        p = glm::vec3(p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx);
        p = glm::vec3(p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy);
        p = glm::vec3(p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z);

        // Blish Z-up -> file/Mumble Y-up.
        const glm::vec3 world = marker.position + glm::vec3(p.x, p.z, p.y);

        glm::vec2 screen;
        if (!camera.project(world, &screen)) return false;
        out[i] = ImVec2(screen.x, screen.y);
    }

    return true;
}

void drawMarkers(editor::State& state, const Camera& camera, ImDrawList* list, std::vector<Sorted>& order) {
    for (const Sorted& entry : order) {
        if (entry.is_trail) continue;

        const MarkerDraw& marker = cache.markers[entry.index];

        glm::vec2 screen;
        if (!camera.project(marker.position, &screen)) continue;

        const float opacity = marker.alpha * fadeFactor(metresToInches(entry.distance), marker.fade_near, marker.fade_far);
        if (opacity <= 0.003f) continue;

        const bool selected =
            state.settings.highlight_3d && state.selection.contains(editor::NodeRef{editor::NodeKind::Pathable, marker.pathable});
        const ImU32 colour = applyOpacity(marker.tint, opacity);
        ImTextureID texture = resources::texture(marker.icon);

        // Drawn back to front, so a later hit is nearer the camera and wins.
        const ImVec2 mouse = ImGui::GetIO().MousePos;

        if (marker.has_rotation) {
            // Fixed in the world, so it foreshortens with the viewing angle. minSize and
            // maxSize do not apply: those clamp a billboard's screen size, and clamping a
            // world-locked quad would detach it from the surface it is placed on.
            ImVec2 corners[4];
            if (!rotatedMarkerCorners(camera, marker, corners)) continue;

            if (texture) {
                // Blish derives each UV from the sign of the vertex it belongs to:
                //     new Vector2(vert.X < 0 ? 1 : 0, vert.Y < 0 ? 1 : 0)
                // so the -X,-Y corner carries uv (1,1) and the +X,+Y corner (0,0). Both
                // axes are inverted relative to the obvious (0,0)->(1,1) mapping, which is
                // exactly a 180-degree in-plane rotation of the image.
                list->AddImageQuad(
                    texture,
                    corners[0],
                    corners[1],
                    corners[2],
                    corners[3],
                    ImVec2(1, 1),
                    ImVec2(0, 1),
                    ImVec2(0, 0),
                    ImVec2(1, 0),
                    colour);
            } else {
                list->AddConvexPolyFilled(corners, 4, colour);
            }

            if (selected) {
                list->AddPolyline(corners, 4, IM_COL32(255, 210, 60, 255), true, 2.0f);
            } else if (marker.pathable == hover_highlight) {
                list->AddPolyline(corners, 4, IM_COL32(255, 255, 255, 160), true, 1.5f);
            }

            if (pointInQuad(mouse, corners)) hovered_pathable = marker.pathable;
            continue;
        }

        const float camera_distance = glm::length(marker.position - camera.position);
        // Blish scales the unit quad by iconSize * 2, then clamps its on-screen height
        // between minSize*4 and maxSize*4 of *its* pixel measure — which is twice the real
        // pixel height. See kBlishPixelScale.
        float pixels = camera.pixelSize(marker.icon_size * 2.0f, camera_distance);
        pixels = std::clamp(pixels, marker.min_size * kBlishSizeLimit, marker.max_size * kBlishSizeLimit);
        if (pixels < 1.0f) continue;

        const float half = pixels * 0.5f;
        const ImVec2 min(screen.x - half, screen.y - half);
        const ImVec2 max(screen.x + half, screen.y + half);

        if (texture) {
            list->AddImage(texture, min, max, ImVec2(0, 0), ImVec2(1, 1), colour);
        } else {
            // No icon, or it failed to load: a dot still shows the marker is there.
            list->AddCircleFilled(ImVec2(screen.x, screen.y), std::max(2.0f, half * 0.35f), applyOpacity(marker.tint, opacity * 0.7f));
        }

        if (mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y) {
            hovered_pathable = marker.pathable;
        }

        if (!selected && marker.pathable == hover_highlight) {
            list->AddRect(
                ImVec2(min.x - 2.0f, min.y - 2.0f),
                ImVec2(max.x + 2.0f, max.y + 2.0f),
                IM_COL32(255, 255, 255, 160),
                0.0f,
                0,
                1.5f);
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
            state.settings.highlight_3d && state.selection.contains(editor::NodeRef{editor::NodeKind::Pathable, trail.pathable});

        TrailDraw effective = trail;
        if (selected) effective.tint = IM_COL32(255, 210, 60, (trail.tint >> IM_COL32_A_SHIFT) & 0xFF);

        for (const pack::trl::Segment& segment : data->segments) {
            drawTrailSection(camera, list, segment, effective, texture);
        }
    }
}

// Handles for the trail open in the editor, drawn over the ribbon: one small dot per
// point, so a point can be aimed at and dragged the way a marker can. Only the open trail
// gets them — every trail at once would be an unreadable field of dots.
void drawTrailPoints(editor::State& state, const Camera& camera, ImDrawList* list, int map_id) {
    constexpr float kHandleRadius = 4.0f;
    constexpr float kPickRadius = 9.0f; // px; a dot is small, so pick with some slack

    if (!trail_edit::isOpen()) return;

    const size_t pathable = trail_edit::pathable();
    if (!state.isVisible(editor::NodeRef{editor::NodeKind::Pathable, pathable})) return;

    // Culled with the trail it belongs to, so the handles cannot outlive the ribbon.
    for (const TrailDraw& trail : cache.trails) {
        if (trail.pathable != pathable) continue;
        if (trail.map_id != map_id) return;
        break;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    float best_distance = kPickRadius;
    size_t best_point = pack::kNoIndex;

    for (size_t i = 0; i < trail_edit::pointCount(); ++i) {
        glm::vec2 screen(0.0f);
        if (!camera.project(trail_edit::point(i), &screen)) continue;
        if (screen.x < 0.0f || screen.y < 0.0f || screen.x > camera.viewport.x || screen.y > camera.viewport.y) continue;

        const ImVec2 at(screen.x, screen.y);
        const bool selected = state.selection.contains(editor::NodeRef{editor::NodeKind::TrailPoint, pathable, i});

        list->AddCircleFilled(at, kHandleRadius, selected ? IM_COL32(255, 210, 60, 255) : IM_COL32(60, 140, 255, 200));
        if (selected || i == hover_point_highlight) {
            list->AddCircle(at, kHandleRadius + 2.0f, IM_COL32(255, 255, 255, selected ? 255 : 160), 12, 1.5f);
        }

        const float dx = mouse.x - at.x;
        const float dy = mouse.y - at.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance < best_distance) {
            best_distance = distance;
            best_point = i;
        }
    }

    hovered_trail_point.store(best_point);
}

} // namespace

size_t hoveredPathable() {
    return hovered_pathable.load();
}

size_t hoveredTrailPoint() {
    return hovered_trail_point.load();
}

void draw(editor::State& state, const Camera& camera) {
    hover_highlight = state.settings.select_in_viewport ? hovered_pathable.load() : pack::kNoIndex;
    hovered_pathable.store(pack::kNoIndex);
    hover_point_highlight = state.settings.select_in_viewport ? hovered_trail_point.load() : pack::kNoIndex;
    hovered_trail_point.store(pack::kNoIndex);

    // With the map up the world is not on screen; markers drawn over it would sit at
    // meaningless positions. Bailing after the hover reset above also means nothing stays
    // hovered, so a click on the map cannot select what was underneath it.
    if (!state.loaded || !camera.valid || camera.map_open) return;

    if (cache.revision != state.revision || cache.pack != &state.pack) rebuild(state);

    // Behind our own windows but over the game.
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    if (!list) return;

    const int map_id = static_cast<int>(camera.map_id);

    std::vector<Sorted> order;
    order.reserve(256);

    for (size_t i = 0; i < cache.markers.size(); ++i) {
        const MarkerDraw& marker = cache.markers[i];
        if (marker.map_id != map_id) continue;
        if (!state.isVisible(editor::NodeRef{editor::NodeKind::Pathable, marker.pathable})) continue;

        const float distance = glm::length(marker.position - camera.player);
        // Cheap reject before any projection work.
        if (marker.fade_far > 0.0f && metresToInches(distance) > marker.fade_far) continue;

        order.push_back(Sorted{distance, i, false});
    }

    for (size_t i = 0; i < cache.trails.size(); ++i) {
        if (cache.trails[i].map_id != map_id) continue;
        if (!state.isVisible(editor::NodeRef{editor::NodeKind::Pathable, cache.trails[i].pathable})) continue;
        // Trails span a large area, so a single distance is only a sorting hint.
        order.push_back(Sorted{0.0f, i, true});
    }

    // Back to front: with no depth buffer, painter's order is all we have.
    std::sort(order.begin(), order.end(), [](const Sorted& a, const Sorted& b) { return a.distance > b.distance; });

    drawTrails(state, camera, list, order);
    // Over the ribbon, under the markers: a handle you cannot see is a handle you cannot
    // click, and a marker sitting on a trail should still win the click.
    drawTrailPoints(state, camera, list, map_id);
    drawMarkers(state, camera, list, order);
}

} // namespace world::renderer
