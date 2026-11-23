#include "viewer/scene.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <atomic>

#include "viewer/bake.h"
#include "viewer/behaviors/update.h"
#include "viewer/ribbon.h"

namespace viewer {

namespace {

std::atomic<std::shared_ptr<const Scene>> latest;

// Below this an item is invisible: under one step of an 8-bit alpha.
constexpr float kInvisibleOpacity = 0.003f;

bool categoryVisible(size_t node, const std::vector<char>& visible) {
    // A pathable whose type names no category has nothing to turn off, so it shows.
    return node >= visible.size() || visible[node];
}

float squaredDistance(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 d = a - b;
    return glm::dot(d, d);
}

} // namespace

void buildScene(
    const runtime::LiveMap& live,
    const std::vector<char>& visible,
    const Camera& camera,
    float seconds,
    bool show_hidden,
    const TextureLookup& texture,
    Scene* out) {
    out->items.clear();

    // With the map up the world is not on screen, and anything drawn would sit at positions
    // that mean nothing over it.
    if (!camera.valid || camera.map_open) return;

    for (const runtime::LiveMarker& live_marker : live.markers) {
        const BakedMarker& marker = live_marker.marker;
        if (live_marker.removed || !marker.in_game_visibility || !categoryVisible(live_marker.node, visible)) continue;
        const bool filtered = live_marker.behavior.filtered;
        if (filtered && !show_hidden) continue;

        // StandardMarker.GetOpacity: a hidden marker shown is half there, whatever its fades.
        const float opacity =
            filtered ? 0.5f
                     : marker.alpha *
                           fadeFactor(metresToInches(glm::distance(marker.position, camera.player)), marker.fade_near, marker.fade_far);
        if (opacity <= kInvisibleOpacity) continue;
        const glm::vec3 position = marker.position + glm::vec3(0.0f, behaviors::bounceOffset(live_marker, seconds), 0.0f);

        // Blish scales the unit quad by iconSize * 2, for either kind of marker. A quad that
        // size reaches at most 0.71 of it from its centre whatever way it is turned, so a
        // centre a full width behind the camera has none of itself in front.
        const float size = marker.icon_size * 2.0f;
        if (glm::dot(position - camera.position, camera.front) < -size) continue;

        const ImTextureID image = texture(live_marker.texture_pack, marker.icon);
        if (!image) continue;

        SceneItem item;
        item.texture = image;
        item.colour = applyOpacity(marker.tint, opacity);
        item.depth = squaredDistance(position, camera.position);
        if (marker.has_rotation) {
            // Fixed in the world, so it foreshortens with the viewing angle. minSize and
            // maxSize clamp a billboard's screen size and do not apply: clamping a
            // world-locked quad would detach it from the surface it is placed on.
            item.kind = SceneItem::Kind::Quad;
            rotatedMarkerQuad(position, marker.rotation, marker.icon_size, item.corners);
            std::copy(std::begin(kRotatedMarkerUv), std::end(kRotatedMarkerUv), item.uv);
        } else {
            item.kind = SceneItem::Kind::Billboard;
            item.centre = position;
            item.size = size;
            item.min_size = marker.min_size;
            item.max_size = marker.max_size;
        }
        out->items.push_back(item);
    }

    // Kept between frames: the quads of one trail run are only needed until they are copied
    // into the scene.
    static std::vector<RibbonQuad> quads;
    for (const runtime::LiveTrail& live_trail : live.trails) {
        const BakedTrail& trail = live_trail.trail;
        if (live_trail.removed || !trail.in_game_visibility || !trail.geometry || !categoryVisible(live_trail.node, visible)) continue;
        if (live_trail.behavior.filtered && !show_hidden) continue;

        const ImTextureID image = texture(live_trail.texture_pack, trail.texture);
        if (!image) continue;

        const float scroll = seconds * trail.anim_speed;
        for (const pack::trl::Segment& segment : trail.geometry->segments) {
            quads.clear();
            appendRibbon(segment.points, kTrailHalfWidth * trail.scale, trail.is_wall, scroll, &quads);

            for (const RibbonQuad& quad : quads) {
                // Faded per quad, from its middle. Pathing fades a trail per pixel; this is
                // the approximation viewer.md describes.
                const glm::vec3 middle = (quad.corners[0] + quad.corners[1] + quad.corners[2] + quad.corners[3]) * 0.25f;
                const float opacity =
                    trail.alpha * fadeFactor(metresToInches(glm::distance(middle, camera.player)), trail.fade_near, trail.fade_far);
                if (opacity <= kInvisibleOpacity) continue;

                SceneItem item;
                item.kind = SceneItem::Kind::Quad;
                item.texture = image;
                item.colour = applyOpacity(trail.tint, opacity);
                std::copy(std::begin(quad.corners), std::end(quad.corners), item.corners);
                std::copy(std::begin(quad.uv), std::end(quad.uv), item.uv);
                item.depth = squaredDistance(middle, camera.position);
                out->items.push_back(item);
            }
        }
    }

    std::sort(out->items.begin(), out->items.end(), [](const SceneItem& a, const SceneItem& b) { return a.depth > b.depth; });
}

bool itemCorners(const Camera& camera, const SceneItem& item, glm::vec3 out_corners[4], const ImVec2** out_uv) {
    if (item.kind == SceneItem::Kind::Quad) {
        std::copy(std::begin(item.corners), std::end(item.corners), out_corners);
        *out_uv = item.uv;
        return true;
    }

    const float size = clampedBillboardSize(camera, item.centre, item.size, item.min_size, item.max_size);
    if (size <= 0.0f || !camera.billboardQuad(item.centre, size, out_corners)) return false;
    *out_uv = kBillboardUv;
    return true;
}

void publish(std::shared_ptr<const Scene> scene) {
    latest.store(std::move(scene));
}

std::shared_ptr<const Scene> published() {
    return latest.load();
}

} // namespace viewer
