#include "viewer/scene.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <atomic>
#include <limits>

#include "viewer/bake.h"
#include "viewer/behaviors/update.h"
#include "viewer/ribbon.h"

namespace viewer {

namespace {

bool behindTerrain(RenderLayer layer, const WorldOptions& options) {
    return layer == RenderLayer::Terrain || (layer == RenderLayer::Default && options.behind_terrain);
}

std::atomic<std::shared_ptr<const Scene>> latest;

// Below this an item is invisible: under one step of an 8-bit alpha.
constexpr float kInvisibleOpacity = 0.003f;

// The longest piece a trail step crossing its fade is cut into: how coarsely it fades out.
constexpr float kFadePieceMetres = 2.0f;

float squaredDistance(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 d = a - b;
    return glm::dot(d, d);
}

// How far `point` is from the nearest point of the segment a→b.
float distanceToSegment(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 along = b - a;
    const float length_squared = glm::dot(along, along);
    const float t = length_squared > 0.0f ? std::clamp(glm::dot(point - a, along) / length_squared, 0.0f, 1.0f) : 0.0f;
    return glm::distance(point, a + along * t);
}

// SceneItem::front for a quad culled as `cull` says, whose first triangle as Blish draws it is
// (v0, v1, v2) — see Cull.
glm::vec3 frontOf(Cull cull, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) {
    const glm::vec3 normal = glm::cross(v1 - v0, v2 - v0);
    switch (cull) {
    case Cull::CounterClockwise: return normal;
    case Cull::Clockwise: return -normal;
    case Cull::None: break;
    }
    return glm::vec3(0.0f);
}

// PathingEntity.AnimatedFadeOpacity: from nothing to whole over FADEIN_DURATION, 800 ms, from
// when the pathable came into view.
float fadeInOf(double in_view_since, float seconds) {
    constexpr double kFadeInSeconds = 0.8;
    return static_cast<float>(std::clamp((seconds - in_view_since) / kFadeInSeconds, 0.0, 1.0));
}

// What marker.hlsl's distance fade leaves of a marker on average, a pixel's distance standing
// in for the marker's: fading as `mult` goes from 0 at fadeNear to 1 at fadeFar, it scales a
// pixel by 1 - mult and dissolves the pixels whose dissolve texture value is under `mult`,
// and clips everything past fadeFar. The near and far it is handed are inches, capped by the
// view distance as StandardMarker.World.Render caps them — which is how Max View Distance,
// meant in metres, ends up cutting every marker off at that many inches.
float shaderFade(float distance_inches, float fade_near, float fade_far, float view_distance) {
    // The share of the dissolve's pixels kept at mult = 0+, 0.05, ..., 1: of Pathing's
    // ref/png/42975.png, the texels whose 0.21 r + 0.71 b + 0.071 g, as marker.hlsl weighs
    // them, is at least mult. A fifth are black, gone as soon as a fade starts.
    constexpr float kKept[21] = {0.801f, 0.742f, 0.659f, 0.596f, 0.554f, 0.527f, 0.495f, 0.467f, 0.442f, 0.414f, 0.390f,
                                 0.361f, 0.333f, 0.298f, 0.269f, 0.224f, 0.177f, 0.135f, 0.095f, 0.064f, 0.000f};

    // Populate_FadeNearAndFar: either negative turns both off, to float.MaxValue.
    if (fade_near < 0.0f || fade_far < 0.0f) fade_near = fade_far = std::numeric_limits<float>::max();
    const float far_inches = std::min(fade_far, view_distance);
    const float near_inches = std::min(fade_near, view_distance - (fade_far - fade_near));

    if (far_inches > 0.0f && distance_inches > far_inches) return 0.0f;
    if (near_inches <= 0.0f || far_inches <= 0.0f || distance_inches <= near_inches) return 1.0f;
    const float mult = far_inches > near_inches ? std::min((distance_inches - near_inches) / (far_inches - near_inches), 1.0f) : 1.0f;
    const float step = mult * 20.0f;
    const int below = std::min(static_cast<int>(step), 19);
    const float kept = kKept[below] + (kKept[below + 1] - kKept[below]) * (step - static_cast<float>(below));
    return (1.0f - mult) * kept;
}

// Appends one layer's items to `out`, unsorted. `settings_opacity` is GetOpacity's settings,
// the same for every marker and trail.
void appendLayer(
    const SceneLayer& layer,
    const Camera& camera,
    float seconds,
    const WorldOptions& options,
    float settings_opacity,
    Scene* out) {
    const runtime::LiveMap& live = *layer.live;
    const std::vector<char>& visible = *layer.visible;

    for (const runtime::LiveMarker& live_marker : live.markers) {
        const BakedMarker& marker = live_marker.marker;
        if (live_marker.removed || !marker.in_game_visibility || !runtime::nodeVisible(live_marker.node, visible)) continue;
        const bool filtered = live_marker.behavior.filtered;
        if (filtered && !options.show_hidden) continue;

        const float distance = glm::distance(marker.position, camera.player);
        if (distance > options.view_distance) continue;
        // StandardMarker.GetOpacity, then the shader's fade over it. A hidden marker shown is
        // half there, whatever GetOpacity's fades; the shader's still apply.
        const float inches = metresToInches(distance);
        const float own_opacity = filtered ? 0.5f
                                           : marker.alpha * fadeFactor(inches, marker.fade_near, marker.fade_far) * settings_opacity *
                                                 fadeInOf(live_marker.in_view_since, seconds);
        const float opacity = own_opacity * shaderFade(inches, marker.fade_near, marker.fade_far, options.view_distance);
        if (opacity <= kInvisibleOpacity) continue;
        if (!marker.has_rotation && marker.cull == Cull::Clockwise) continue;
        const glm::vec3 position = marker.position + glm::vec3(0.0f, behaviors::bounceOffset(live_marker, seconds), 0.0f);

        // Blish scales the unit quad by iconSize * 2, for either kind of marker. A quad that
        // size reaches at most 0.71 of it from its centre whatever way it is turned, so a
        // centre a full width behind the camera has none of itself in front.
        const float size = marker.icon_size * 2.0f * options.marker_scale;
        if (glm::dot(position - camera.position, camera.front) < -size) continue;

        const ImTextureID image = layer.texture(live_marker.texture_pack, marker.icon);
        if (!image && !layer.edited) continue;

        SceneItem item;
        item.texture = image;
        if (layer.edited) item.pathable = marker.pathable;
        item.colour = applyOpacity(marker.tint, opacity);
        // StandardMarker.DrawOrder: from where the marker stands, its heightOffset and bounce
        // (which BounceModifier tweens into the heightOffset) left out. Hero's pack draws
        // markers at one spot from positions that differ, and relies on this to stack them.
        item.depth = squaredDistance(marker.position - glm::vec3(0.0f, marker.height_offset, 0.0f), camera.position);
        item.behind_terrain = behindTerrain(marker.render_layer, options);
        if (marker.has_rotation) {
            // Fixed in the world, so it foreshortens with the viewing angle. minSize and
            // maxSize clamp a billboard's screen size and do not apply: clamping a
            // world-locked quad would detach it from the surface it is placed on.
            item.kind = SceneItem::Kind::Quad;
            rotatedMarkerQuad(position, marker.rotation, marker.icon_size * options.marker_scale, item.corners);
            std::copy(std::begin(kRotatedMarkerUv), std::end(kRotatedMarkerUv), item.uv);
            // Blish's first triangle: its -X-Y, +X-Y and -X+Y corners.
            item.front = frontOf(marker.cull, item.corners[0], item.corners[1], item.corners[3]);
        } else {
            item.kind = SceneItem::Kind::Billboard;
            item.centre = position;
            item.size = size;
            item.min_size = marker.min_size * options.marker_scale;
            item.max_size = marker.max_size;
        }
        out->items.push_back(item);
    }

    // Kept between calls: the quads of one trail run are only needed until they are copied
    // into the scene.
    static std::vector<RibbonQuad> quads;
    static std::vector<RibbonQuad> pieces;
    for (const runtime::LiveTrail& live_trail : live.trails) {
        const BakedTrail& trail = live_trail.trail;
        if (live_trail.removed || !trail.in_game_visibility || !trail.geometry || !runtime::nodeVisible(live_trail.node, visible)) continue;
        if (live_trail.behavior.filtered && !options.show_hidden) continue;

        const ImTextureID image = layer.texture(live_trail.texture_pack, trail.texture);
        if (!image && !layer.edited) continue;

        // StandardTrail.World.Render: the view distance caps the fade, in the fade's inches.
        const float fade_far = std::min(trail.fade_far, options.view_distance);
        const float fade_near = std::min(trail.fade_near, options.view_distance - (trail.fade_far - trail.fade_near));
        const float scroll = seconds * std::min(trail.anim_speed, options.max_trail_speed);
        const float fade_in = fadeInOf(live_trail.in_view_since, seconds);
        const auto fadeAt = [&](float metres) { return fadeFactor(metresToInches(metres), fade_near, fade_far); };
        // `fade` is the quad's distance fade, from fadeNear to fadeFar. Pathing's trail.hlsl
        // never reads fadeNear, so its trails stay whole up to fadeFar and are cut off there;
        // fading them out instead is ours, on purpose.
        const auto appendQuad = [&](const RibbonQuad& quad, float fade) {
            const float opacity = trail.alpha * fade * settings_opacity * fade_in;
            if (opacity <= kInvisibleOpacity) return;

            SceneItem item;
            item.kind = SceneItem::Kind::Quad;
            item.texture = image;
            item.colour = applyOpacity(trail.tint, opacity);
            std::copy(std::begin(quad.corners), std::end(quad.corners), item.corners);
            std::copy(std::begin(quad.uv), std::end(quad.uv), item.uv);
            // Blish's strip starts each quad at its right corner at the first point, then its
            // left, then the right at the next — see appendRibbon's corner order.
            item.front = frontOf(trail.cull, quad.corners[1], quad.corners[0], quad.corners[2]);
            // StandardTrail.DrawOrder is float.MaxValue: every trail under every marker, trails
            // in the order loaded.
            item.depth = std::numeric_limits<float>::max();
            item.behind_terrain = behindTerrain(trail.render_layer, options);
            if (layer.edited) {
                item.pathable = trail.pathable;
                item.is_trail = true;
            }
            out->items.push_back(item);
        };
        for (const pack::trl::Segment& segment : trail.geometry->segments) {
            quads.clear();
            appendRibbon(segment.points, kTrailHalfWidth * trail.scale, trail.is_wall, scroll, &quads);

            for (const RibbonQuad& step : quads) {
                // A step is one quad per point and may be hundreds of metres long. Where it fades
                // alike from where it comes nearest the player to where it goes farthest, it is
                // drawn whole; where not, it is cut, so it shows up to fadeFar however far its
                // middle is, and fades a piece at a time.
                const glm::vec3 start = (step.corners[0] + step.corners[1]) * 0.5f;
                const glm::vec3 end = (step.corners[2] + step.corners[3]) * 0.5f;
                const float nearest = fadeAt(distanceToSegment(camera.player, start, end));
                if (nearest <= 0.0f) continue;
                if (fadeAt(std::max(glm::distance(camera.player, start), glm::distance(camera.player, end))) == nearest) {
                    appendQuad(step, nearest);
                    continue;
                }
                pieces.clear();
                cutStep(step, kFadePieceMetres, &pieces);
                for (const RibbonQuad& piece : pieces) {
                    const glm::vec3 middle = (piece.corners[0] + piece.corners[1] + piece.corners[2] + piece.corners[3]) * 0.25f;
                    appendQuad(piece, fadeAt(glm::distance(middle, camera.player)));
                }
            }
        }
    }
}

} // namespace

void buildScene(const std::vector<SceneLayer>& layers, const Camera& camera, float seconds, const WorldOptions& options, Scene* out) {
    out->items.clear();

    // With the map up the world is not on screen, and anything drawn would sit at positions
    // that mean nothing over it.
    if (!camera.valid || camera.map_open || !options.enabled) return;

    const float settings_opacity = options.max_opacity * (options.combat_fade && options.in_combat ? 0.5f : 1.0f);
    for (const SceneLayer& layer : layers) appendLayer(layer, camera, seconds, options, settings_opacity, out);

    // Stable, as Blish's StableSortKey breaks a tie by its entity list: markers stacked on one
    // spot are drawn in the order they were loaded.
    std::stable_sort(out->items.begin(), out->items.end(), [](const SceneItem& a, const SceneItem& b) { return a.depth > b.depth; });
}

void buildScene(
    const runtime::LiveMap& live,
    const std::vector<char>& visible,
    const Camera& camera,
    float seconds,
    const WorldOptions& options,
    const TextureLookup& texture,
    Scene* out) {
    buildScene({SceneLayer{&live, &visible, texture, false}}, camera, seconds, options, out);
}

bool itemCorners(const Camera& camera, const SceneItem& item, glm::vec3 out_corners[4], const ImVec2** out_uv) {
    if (item.kind == SceneItem::Kind::Quad) {
        if (item.front != glm::vec3(0.0f) && glm::dot(item.front, camera.position - item.corners[0]) <= 0.0f) return false;
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
