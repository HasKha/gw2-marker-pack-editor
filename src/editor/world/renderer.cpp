#include "editor/world/renderer.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "editor/resources.h"
#include "editor/trail_edit.h"
#include "viewer/bake.h"
#include "viewer/render/overlay.h"
#include "viewer/ribbon.h"

namespace world::renderer {

namespace {

// Resolved once per pack revision so the per-frame loop never walks the category chain.
// The largest pack tested has 55,887 markers; doing inherited-attribute lookups for each
// of them every frame is not viable inside the render callback.
//
// Keyed on the revision alone. That is enough for a new pack too: State::reset bumps it
// precisely so caches like this one cannot survive into the next pack.
struct Cache {
    uint64_t revision = 0;
    viewer::BakedPathables baked;
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

bool pointInPolygon(const ImVec2& p, const ImVec2* polygon, int count) {
    // Sign of the cross product against each edge; consistent sign means inside. Works
    // for any convex polygon, which a projected marker always is.
    bool positive = false;
    bool negative = false;
    for (int i = 0; i < count; ++i) {
        const ImVec2& a = polygon[i];
        const ImVec2& b = polygon[(i + 1) % count];
        const float cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (cross > 0.0f) positive = true;
        if (cross < 0.0f) negative = true;
        if (positive && negative) return false;
    }
    return true;
}

struct Sorted {
    float distance = 0.0f;
    size_t index = 0;
    bool is_trail = false;
};

// The picking test and the outlines, which are the same for a marker of either kind once
// its corners are known. `count` is the *visible* corner count, so what you can click is
// what is on screen — a marker you are standing inside is picked over the part of it you
// can actually see.
void markerQuadOverlay(ImDrawList* list, const ImVec2* corners, int count, size_t pathable, bool selected) {
    // Drawn back to front, so a later hit is nearer the camera and wins.
    if (pointInPolygon(ImGui::GetIO().MousePos, corners, count)) hovered_pathable = pathable;

    if (selected) {
        list->AddPolyline(corners, count, IM_COL32(255, 210, 60, 255), true, 2.0f);
    } else if (pathable == hover_highlight) {
        list->AddPolyline(corners, count, IM_COL32(255, 255, 255, 160), true, 1.5f);
    }
}

void drawMarkers(editor::State& state, const viewer::Camera& camera, ImDrawList* list, std::vector<Sorted>& order) {
    for (const Sorted& entry : order) {
        if (entry.is_trail) continue;

        const viewer::BakedMarker& marker = cache.baked.markers[entry.index];

        // Blish scales the unit quad by iconSize * 2, for either kind of marker.
        const float world_size = marker.icon_size * 2.0f;

        // Cheap reject before any quad work, and the *only* thing here that decides on the
        // marker's centre. A quad `world_size` across reaches at most 0.71 of that from its
        // centre whatever way it is turned, so a centre a full width behind the camera
        // cannot have any of itself in front of one. Rejecting on the centre alone — which
        // is what this used to do — is what made a large marker vanish the moment you
        // walked past its middle, with most of it still on screen.
        if (glm::dot(marker.position - camera.position, camera.front) < -world_size) continue;

        const float opacity = marker.alpha * viewer::fadeFactor(viewer::metresToInches(entry.distance), marker.fade_near, marker.fade_far);
        if (opacity <= 0.003f) continue;

        const bool selected =
            state.settings.highlight_3d && state.selection.contains(editor::NodeRef{editor::NodeKind::Pathable, marker.pathable});
        const ImU32 colour = viewer::applyOpacity(marker.tint, opacity);
        ImTextureID texture = resources::texture(marker.icon);

        glm::vec3 world_quad[4];
        viewer::overlay::ClippedQuad visible;
        ImVec2 corners[5];

        if (marker.has_rotation) {
            // Fixed in the world, so it foreshortens with the viewing angle. minSize and
            // maxSize do not apply: those clamp a billboard's screen size, and clamping a
            // world-locked quad would detach it from the surface it is placed on.
            viewer::rotatedMarkerQuad(marker.position, marker.rotation, marker.icon_size, world_quad);
            if (!viewer::overlay::clipAndProject(camera, world_quad, &visible)) continue;
            viewer::overlay::screenCorners(visible, corners);

            if (texture) {
                viewer::overlay::drawTexturedQuad(list, camera, texture, visible, viewer::kRotatedMarkerUv, colour);
            } else {
                list->AddConvexPolyFilled(corners, visible.count, colour);
            }

            markerQuadOverlay(list, corners, visible.count, marker.pathable, selected);
            continue;
        }

        // Billboarded towards the camera and projected — see Camera::billboardCorners for
        // why neither the size nor the shape of that follows from the distance alone — at
        // the size minSize and maxSize allow.
        const float drawn_size = viewer::clampedBillboardSize(camera, marker.position, world_size, marker.min_size, marker.max_size);
        if (drawn_size <= 0.0f) continue;
        if (!camera.billboardQuad(marker.position, drawn_size, world_quad)) continue;

        if (!viewer::overlay::clipAndProject(camera, world_quad, &visible)) continue;
        viewer::overlay::screenCorners(visible, corners);

        glm::vec2 centre;
        if (texture) {
            viewer::overlay::drawTexturedQuad(list, camera, texture, visible, viewer::kBillboardUv, colour);
        } else if (camera.project(marker.position, &centre)) {
            // No icon, or it failed to load: a dot still shows the marker is there. A dot
            // and not the whole quad, deliberately — an icon-less marker scaled up should
            // not blanket the screen with a solid colour.
            const float radius = std::max(2.0f, viewer::quadPixelHeight(visible.screen, visible.count) * 0.175f);
            list->AddCircleFilled(ImVec2(centre.x, centre.y), radius, viewer::applyOpacity(marker.tint, opacity * 0.7f));
        }

        markerQuadOverlay(list, corners, visible.count, marker.pathable, selected);
    }
}

// One run of a trail's points as the ribbon Pathing draws, each quad built in world space and
// projected corner by corner so the width is perspective-correct.
void drawTrailRun(
    const viewer::Camera& camera,
    ImDrawList* list,
    const pack::trl::Segment& segment,
    const viewer::BakedTrail& trail,
    ImTextureID texture) {
    // Kept between calls: a frame draws many runs, and the quads of each are only needed
    // until it is drawn.
    static std::vector<viewer::RibbonQuad> quads;
    quads.clear();
    const float scroll = static_cast<float>(ImGui::GetTime()) * trail.anim_speed;
    viewer::appendRibbon(segment.points, viewer::kTrailHalfWidth * trail.scale, trail.is_wall, scroll, &quads);

    for (const viewer::RibbonQuad& quad : quads) {
        // Clipped rather than skipped: a trail you are standing on has the quad under your
        // feet straddling the near plane, and dropping it leaves a gap in the ribbon exactly
        // where you are looking.
        viewer::overlay::ClippedQuad visible;
        if (!viewer::overlay::clipAndProject(camera, quad.corners, &visible)) continue;

        // Fade is measured from the player, not the camera — same as markers, and same as
        // Blish's DistanceToPlayer.
        const glm::vec3 middle = (quad.corners[0] + quad.corners[1] + quad.corners[2] + quad.corners[3]) * 0.25f;
        const float distance = glm::length(middle - camera.player);
        const float opacity = trail.alpha * viewer::fadeFactor(viewer::metresToInches(distance), trail.fade_near, trail.fade_far);
        if (opacity <= 0.003f) continue;
        const ImU32 colour = viewer::applyOpacity(trail.tint, opacity);

        if (texture) {
            // A quad running away from the camera has its two ends at very different depths,
            // which is the case the subdivision exists for: without it the texture slides
            // along the ribbon instead of compressing into the distance.
            viewer::overlay::drawTexturedQuad(list, camera, texture, visible, quad.uv, colour);
        } else {
            // Pathing draws nothing for a trail without a texture; the editor shows where it
            // runs, so that it can be fixed.
            ImVec2 corners[5];
            viewer::overlay::screenCorners(visible, corners);
            list->AddConvexPolyFilled(corners, visible.count, colour);
        }
    }
}

// No editor::State: a ribbon is drawn from its cached attributes and nothing else. Which
// trails are in `order` at all is decided in draw(), selection included.
void drawTrails(const viewer::Camera& camera, ImDrawList* list, std::vector<Sorted>& order) {
    for (const Sorted& entry : order) {
        if (!entry.is_trail) continue;

        const viewer::BakedTrail& trail = cache.baked.trails[entry.index];
        const pack::trl::Trail* data = resources::trail(trail.data);
        if (!data) continue;

        ImTextureID texture = resources::texture(trail.texture);

        // Selecting a trail deliberately leaves the ribbon alone. Repainting a whole trail
        // gold is a lot of screen to change for one bit of information, and it hides the very
        // thing you selected it to look at — its texture, its tint, where it fades. The point
        // handles in drawTrailPoints are the selection feedback.
        for (const pack::trl::Segment& segment : data->segments) {
            drawTrailRun(camera, list, segment, trail, texture);
        }
    }
}

// Handles for the trail open in the editor, drawn over the ribbon: one small dot per
// point, so a point can be aimed at and dragged the way a marker can. Only the open trail
// gets them — every trail at once would be an unreadable field of dots.
void drawTrailPoints(editor::State& state, const viewer::Camera& camera, ImDrawList* list, int map_id) {
    constexpr float kHandleRadius = 4.0f;
    constexpr float kPickRadius = 9.0f; // px; a dot is small, so pick with some slack

    if (!trail_edit::isOpen()) return;

    const size_t pathable = trail_edit::pathable();
    if (!state.isVisible(editor::NodeRef{editor::NodeKind::Pathable, pathable})) return;

    // Culled with the trail it belongs to, so the handles cannot outlive the ribbon.
    for (const viewer::BakedTrail& trail : cache.baked.trails) {
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
        const bool selected = state.points.indices.contains(i);

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

void draw(editor::State& state, const viewer::Camera& camera) {
    hover_highlight = state.settings.select_in_viewport ? hovered_pathable.load() : pack::kNoIndex;
    hovered_pathable.store(pack::kNoIndex);
    hover_point_highlight = state.settings.select_in_viewport ? hovered_trail_point.load() : pack::kNoIndex;
    hovered_trail_point.store(pack::kNoIndex);

    // With the map up the world is not on screen; markers drawn over it would sit at
    // meaningless positions. Bailing after the hover reset above also means nothing stays
    // hovered, so a click on the map cannot select what was underneath it.
    if (!state.loaded || !camera.valid || camera.map_open) return;

    if (cache.revision != state.revision) {
        cache.baked = viewer::bake(state.pack);
        cache.revision = state.revision;
    }

    // Behind our own windows but over the game.
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    if (!list) return;

    const int map_id = static_cast<int>(camera.map_id);

    std::vector<Sorted> order;
    order.reserve(256);

    for (size_t i = 0; i < cache.baked.markers.size(); ++i) {
        const viewer::BakedMarker& marker = cache.baked.markers[i];
        if (marker.map_id != map_id) continue;
        if (!state.isVisible(editor::NodeRef{editor::NodeKind::Pathable, marker.pathable})) continue;

        const float distance = glm::length(marker.position - camera.player);
        // Cheap reject before any projection work.
        if (marker.fade_far > 0.0f && viewer::metresToInches(distance) > marker.fade_far) continue;

        order.push_back(Sorted{distance, i, false});
    }

    for (size_t i = 0; i < cache.baked.trails.size(); ++i) {
        viewer::BakedTrail& trail = cache.baked.trails[i];

        // Its map lives in the .trl header, so the file has to have been read. Cached in the
        // BakedTrail once it has: an already-parsed trail is a hash lookup that costs no
        // budget, so a rebuild re-resolving every trail is free, and only a genuinely unread
        // one has to wait its turn. Until then it is on no map, which is the same thing
        // drawTrails does with a trail whose geometry has not arrived.
        if (trail.map_id == viewer::kMapUnknown) {
            const pack::trl::Trail* data = resources::trail(trail.data);
            if (!data) continue;
            trail.map_id = data->map_id;
        }

        if (trail.map_id != map_id) continue;
        if (!state.isVisible(editor::NodeRef{editor::NodeKind::Pathable, trail.pathable})) continue;
        // Trails span a large area, so a single distance is only a sorting hint.
        order.push_back(Sorted{0.0f, i, true});
    }

    // Back to front: with no depth buffer, painter's order is all we have.
    std::sort(order.begin(), order.end(), [](const Sorted& a, const Sorted& b) { return a.distance > b.distance; });

    drawTrails(camera, list, order);
    // Over the ribbon, under the markers: a handle you cannot see is a handle you cannot
    // click, and a marker sitting on a trail should still win the click.
    drawTrailPoints(state, camera, list, map_id);
    drawMarkers(state, camera, list, order);
}

} // namespace world::renderer
