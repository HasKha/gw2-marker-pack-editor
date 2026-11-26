#include "editor/world/overlay.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>

#include "editor/trail_edit.h"
#include "viewer/live/bake.h"
#include "viewer/render/overlay.h"

namespace world::overlay {

namespace {

// Frontmost marker under the mouse, found by draw(). Atomic because the WndProc callback
// reads it from the game's message thread.
std::atomic<size_t> hovered_pathable{pack::kNoIndex};

// What to outline as hovered this frame. The live value is only complete once every marker
// has been tested, so the highlight uses the previous frame's result — a frame of latency
// nobody can see, and it makes picking visible instead of a guess.
size_t hover_highlight = pack::kNoIndex;

// The same, for the points of the trail open in the editor. They are drawn as handles because
// a coordinate has nothing to click otherwise.
std::atomic<size_t> hovered_trail_point{pack::kNoIndex};
size_t hover_point_highlight = pack::kNoIndex;

bool pointInPolygon(const ImVec2& p, const ImVec2* polygon, int count) {
    // Sign of the cross product against each edge; consistent sign means inside. Works for
    // any convex polygon, which a projected marker always is.
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

// Where an item without its image still shows. Pathing draws nothing for it; the editor shows
// where it is, so that it can be fixed. A billboard gets a dot, not its whole quad: an
// icon-less marker scaled up should not blanket the screen with a solid colour.
void drawMissingImage(ImDrawList* list, const viewer::SceneItem& item, const viewer::overlay::ClippedQuad& visible, const ImVec2* corners) {
    if (item.kind == viewer::SceneItem::Kind::Quad) {
        list->AddConvexPolyFilled(corners, visible.count, item.colour);
        return;
    }
    ImVec2 centre(0.0f, 0.0f);
    for (int i = 0; i < visible.count; ++i) centre = ImVec2(centre.x + corners[i].x, centre.y + corners[i].y);
    const float count = static_cast<float>(visible.count);
    centre = ImVec2(centre.x / count, centre.y / count);
    const float radius = std::max(2.0f, viewer::quadPixelHeight(visible.screen, visible.count) * 0.175f);
    list->AddCircleFilled(centre, radius, viewer::applyOpacity(item.colour, 0.7f));
}

// Handles for the trail open in the editor, drawn over the ribbon: one small dot per point, so
// a point can be aimed at and dragged the way a marker can. Only the open trail gets them —
// every trail at once would be an unreadable field of dots.
void drawTrailPoints(editor::State& state, const viewer::Camera& camera, ImDrawList* list) {
    constexpr float kHandleRadius = 4.0f;
    constexpr float kPickRadius = 9.0f; // px; a dot is small, so pick with some slack

    if (!trail_edit::isOpen()) return;
    // Culled with the trail they belong to, so the handles cannot outlive the ribbon.
    if (!state.isVisible(editor::NodeRef{editor::NodeKind::Pathable, trail_edit::pathable()})) return;
    if (trail_edit::mapId() != static_cast<int32_t>(camera.map_id)) return;

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

        const float distance = std::hypot(mouse.x - at.x, mouse.y - at.y);
        if (distance < best_distance) {
            best_distance = distance;
            best_point = i;
        }
    }

    hovered_trail_point.store(best_point);
}

} // namespace

void clearHovered() {
    hovered_pathable.store(pack::kNoIndex);
    hovered_trail_point.store(pack::kNoIndex);
}

size_t hoveredPathable() {
    return hovered_pathable.load();
}

size_t hoveredTrailPoint() {
    return hovered_trail_point.load();
}

void draw(editor::State& state, const viewer::Camera& camera, const viewer::Scene& scene) {
    hover_highlight = state.settings.select_in_viewport ? hovered_pathable.load() : pack::kNoIndex;
    hover_point_highlight = state.settings.select_in_viewport ? hovered_trail_point.load() : pack::kNoIndex;
    clearHovered();
    if (!state.loaded) return;

    // Behind our own windows but over the game.
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    if (!list) return;

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    size_t hovered = pack::kNoIndex;

    // Back to front, so a later hit is nearer the camera and wins.
    for (const viewer::SceneItem& item : scene.items) {
        if (item.pathable == pack::kNoIndex) continue;
        // Selecting a trail leaves its ribbon alone: the point handles are its feedback, and
        // gold over the whole ribbon would hide its texture, tint and fades, what it was
        // selected to look at. Only one without a texture needs anything here.
        if (item.is_trail && item.texture) continue;

        glm::vec3 world[4];
        const ImVec2* uv = nullptr;
        viewer::overlay::ClippedQuad visible;
        if (!viewer::itemCorners(camera, item, world, &uv) || !viewer::overlay::clipAndProject(camera, world, &visible)) continue;
        ImVec2 corners[5];
        viewer::overlay::screenCorners(visible, corners);

        if (!item.texture) drawMissingImage(list, item, visible, corners);
        if (item.is_trail) continue;

        // The visible corners only, so what you can click is what is on screen: a marker you
        // are standing inside is picked by the part of it you can see.
        if (pointInPolygon(mouse, corners, visible.count)) hovered = item.pathable;
        const bool selected =
            state.settings.highlight_3d && state.selection.contains(editor::NodeRef{editor::NodeKind::Pathable, item.pathable});
        if (selected) {
            list->AddPolyline(corners, visible.count, IM_COL32(255, 210, 60, 255), true, 2.0f);
        } else if (item.pathable == hover_highlight) {
            list->AddPolyline(corners, visible.count, IM_COL32(255, 255, 255, 160), true, 1.5f);
        }
    }
    hovered_pathable.store(hovered);

    drawTrailPoints(state, camera, list);
}

} // namespace world::overlay
