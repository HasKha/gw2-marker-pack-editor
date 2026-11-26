#include "viewer/render/scene.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>

#include "viewer/behaviors/update.h"
#include "viewer/live/bake.h"
#include "viewer/render/ribbon.h"

namespace viewer {

namespace {

std::atomic<std::shared_ptr<const Scene>> latest;

// Below this an item is invisible: under one step of an 8-bit alpha.
constexpr float kInvisibleOpacity = 0.003f;

// The longest piece a trail step crossing its fade is cut into: how coarsely it fades out.
constexpr float kFadePieceMetres = 2.0f;

float squaredDistance(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 d = a - b;
    return glm::dot(d, d);
}

// How far `point` is from the nearest point of the box.
float distanceToBox(const glm::vec3& point, const glm::vec3& min, const glm::vec3& max) {
    return glm::length(glm::max(glm::max(min - point, point - max), glm::vec3(0.0f)));
}

// What the camera may show by the time the scene is drawn: its view, widened. The scene is built
// at one Present and drawn at the next frame's moments with that frame's camera, so the view it
// is built with is a frame or so old. Only what lies wholly outside the widened view is left out:
// whatever is partly in it, in front of the camera and behind it at once included, is drawn whole,
// as it is drawn without the test.
class ViewBounds
{
public:
    explicit ViewBounds(const Camera& camera) {
        // How far the camera may turn between the view the scene is built with and the one it is
        // drawn with, a frame or two: 20° is 600° a second at 60 fps over two frames, past a
        // mouse flick. Turned faster, what comes in at the screen's edge shows a frame late.
        constexpr float kTurnRadians = 0.35f;
        // How far it may move meanwhile, with room to spare: a frame at any mount's speed is
        // well under this.
        constexpr float kMoveMetres = 5.0f;
        // Short of a right angle, where a side of the view would fold onto the camera's plane.
        constexpr float kMostHalfAngle = 1.5f;

        const glm::vec3 side = glm::cross(camera.up, camera.front);
        if (!camera.valid || glm::length(side) < 1e-4f || camera.viewport.y <= 0.0f) return;
        const glm::vec3 right = glm::normalize(side);
        const glm::vec3 up = glm::cross(camera.front, right);

        const float half_tall = std::min(camera.fov * 0.5f + kTurnRadians, kMostHalfAngle);
        const float half_wide = std::min(std::atan(std::tan(camera.fov * 0.5f) * camera.aspect) + kTurnRadians, kMostHalfAngle);
        // Each side's normal, pointing in.
        normals_[0] = camera.front * std::sin(half_wide) - right * std::cos(half_wide);
        normals_[1] = camera.front * std::sin(half_wide) + right * std::cos(half_wide);
        normals_[2] = camera.front * std::sin(half_tall) - up * std::cos(half_tall);
        normals_[3] = camera.front * std::sin(half_tall) + up * std::cos(half_tall);
        apex_ = camera.position;
        slack_ = kMoveMetres;
        // The turn's room as pixels at the middle of the screen.
        margin_pixels_ = kTurnRadians * camera.viewport.y / camera.fov;
        enabled_ = true;
    }

    bool sphereOutside(const glm::vec3& centre, float radius) const {
        if (!enabled_) return false;
        for (const glm::vec3& normal : normals_) {
            if (glm::dot(normal, centre - apex_) < -(radius + slack_)) return true;
        }
        return false;
    }

    bool boxOutside(const glm::vec3& min, const glm::vec3& max) const {
        if (!enabled_) return false;
        for (const glm::vec3& normal : normals_) {
            // The box's corner farthest along the normal, which is in if any corner is.
            const glm::vec3 corner(normal.x >= 0.0f ? max.x : min.x, normal.y >= 0.0f ? max.y : min.y, normal.z >= 0.0f ? max.z : min.z);
            if (glm::dot(normal, corner - apex_) < -slack_) return true;
        }
        return false;
    }

    // How big on screen minSize may make a billboard and still leave the turn's room to the
    // test: minSize clamps a billboard's pixels whatever its size in the world, so one far away
    // can be much larger than its sphere.
    float mostMinSize() const { return enabled_ ? margin_pixels_ * 0.5f : 0.0f; }

private:
    bool enabled_ = false; // false leaves nothing out: no camera to widen
    glm::vec3 apex_{0.0f};
    glm::vec3 normals_[4];
    float slack_ = 0.0f;
    float margin_pixels_ = 0.0f;
};

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

// Appends one layer's markers to `markers` and its trails to `out`, unsorted. `settings_opacity` is GetOpacity's settings,
// the same for every marker and trail.
void appendLayer(
    const SceneLayer& layer,
    const Camera& camera,
    float seconds,
    const WorldOptions& options,
    float settings_opacity,
    const ViewBounds& bounds,
    std::vector<SceneItem>* markers,
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

        const ImTextureID image = keptImage(layer.texture, layer.edited, live_marker.texture_pack, marker.icon, &live_marker.image);
        if (!image && !layer.edited) continue;
        // Out of view only after its icon is asked for, so that an icon just beside the view is
        // read before the camera turns to it. A billboard minSize may blow up is never out.
        const bool may_grow = !marker.has_rotation && marker.min_size * options.marker_scale > bounds.mostMinSize();
        if (!may_grow && bounds.sphereOutside(position, size)) continue;

        SceneItem item;
        item.texture = image;
        if (layer.edited) item.pathable = marker.pathable;
        item.colour = applyOpacity(marker.tint, opacity);
        // StandardMarker.DrawOrder: from where the marker stands, its heightOffset and bounce
        // (which BounceModifier tweens into the heightOffset) left out. Hero's pack draws
        // markers at one spot from positions that differ, and relies on this to stack them.
        item.depth = squaredDistance(marker.position - glm::vec3(0.0f, marker.height_offset, 0.0f), camera.position);
        item.occlusion = marker.occlusion.value_or(options.occlusion);
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
        markers->push_back(item);
    }

    std::vector<RibbonQuad>& pieces = out->workspace.pieces;
    for (const runtime::LiveTrail& live_trail : live.trails) {
        const BakedTrail& trail = live_trail.trail;
        if (live_trail.removed || !trail.in_game_visibility || !trail.geometry || !runtime::nodeVisible(live_trail.node, visible)) continue;
        if (live_trail.behavior.filtered && !options.show_hidden) continue;

        const ImTextureID image = keptImage(layer.texture, layer.edited, live_trail.texture_pack, trail.texture, &live_trail.image);
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
            item.occlusion = trail.occlusion.value_or(options.occlusion);
            if (layer.edited) {
                item.pathable = trail.pathable;
                item.is_trail = true;
            }
            out->items.push_back(item);
        };
        if (!live_trail.ribbon) {
            live_trail.ribbon =
                std::make_shared<const TrailRibbon>(buildRibbon(trail.geometry->segments, kTrailHalfWidth * trail.scale, trail.is_wall));
        }
        const TrailRibbon& ribbon = *live_trail.ribbon;
        // A fade only falls with distance, so a box whose nearest point has faded out holds
        // nothing to draw.
        const auto boxShows = [&](const glm::vec3& min, const glm::vec3& max) {
            return fadeAt(distanceToBox(camera.player, min, max)) > 0.0f && !bounds.boxOutside(min, max);
        };
        if (ribbon.quads.empty() || !boxShows(ribbon.min, ribbon.max)) continue;

        for (const TrailRibbon::Stretch& stretch : ribbon.stretches) {
            if (!boxShows(stretch.min, stretch.max)) continue;
            for (size_t i = stretch.first; i < stretch.first + stretch.count; ++i) {
                const RibbonQuad step = scrolled(ribbon.quads[i], scroll);
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

    // Built with the map up too. The world stays on screen while the map opens and closes, and
    // GoodRender draws it under the game's UI, which the map is part of; the overlay, over that UI,
    // skips it itself (drawWorld). Not on a loading screen, whatever MumbleLink still says.
    if (!camera.valid || !options.in_gameplay || !options.enabled) return;

    const float settings_opacity = options.max_opacity * (options.combat_fade && options.in_combat ? 0.5f : 1.0f);
    const ViewBounds bounds(camera);
    std::vector<SceneItem>& markers = out->workspace.markers;
    std::vector<DepthKey>& keys = out->workspace.keys;
    markers.clear();
    for (const SceneLayer& layer : layers) appendLayer(layer, camera, seconds, options, settings_opacity, bounds, &markers, out);

    // Every trail under every marker, in the order loaded, as StandardTrail.DrawOrder's float.MaxValue puts them; then
    // the markers back to front, ties in the order loaded, as Blish's StableSortKey breaks them by its entity list:
    // markers stacked on one spot are drawn in the order they were loaded.
    backToFront(markers, &keys);
    out->items.reserve(out->items.size() + markers.size());
    for (const DepthKey& key : keys) out->items.push_back(markers[key.index]);
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
