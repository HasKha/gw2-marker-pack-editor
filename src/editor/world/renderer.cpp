#include "editor/world/renderer.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "editor/resources.h"
#include "editor/trail_edit.h"

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

    // See MarkerTransform in the header: the presence of any rotate attribute is what
    // turns the marker from a billboard into a fixed-orientation quad.
    bool has_rotation = false;
    glm::vec3 rotation{0.0f}; // radians
};

// No real map has this id, so it is safe as "not resolved yet" — see TrailDraw::map_id.
constexpr int kMapUnknown = -1;

struct TrailDraw {
    size_t pathable = pack::kNoIndex;

    // **From the `.trl` header, not from the XML.** `UnpackTrailPoi` reads the header and
    // then does `AddOrUpdateAttribute("mapid", ...)`, overwriting whatever the XML said, so
    // the header is what decides which map Blish draws a trail on and the attribute is dead
    // weight. Culling on the attribute would put a trail whose two disagree on a different
    // map than Blish puts it — see docs/format.md.
    //
    // Which means it cannot be resolved in rebuild(): reading a `.trl` goes through the
    // per-frame load budget and comes back empty until the file's turn arrives. So it starts
    // unknown and is filled in by the draw loop on the first frame the file is there.
    int map_id = kMapUnknown;

    float alpha = 1.0f;
    float scale = 1.0f;
    bool is_wall = false;
    float fade_near = 700.0f;
    float fade_far = 900.0f;
    ImU32 tint = IM_COL32_WHITE;
    std::string texture;
    std::string data;
};

// Keyed on the revision alone. That is enough for a new pack too: State::reset bumps it
// precisely so caches like this one cannot survive into the next pack.
struct Cache {
    uint64_t revision = 0;
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

// ImGui has no w to divide by. AddImageQuad rasterizes two triangles and interpolates the
// UVs affinely across them, which is only correct when all four corners sit at the same
// depth — and they never do. A billboard is perpendicular to the ray to its own centre, so
// the further off-axis it sits the more its corners differ in view-space depth, and a
// marker carrying `rotate` is tilted by construction. What comes out is a texture bent
// along the quad's diagonal rather than foreshortened: on a large marker, a saddle.
//
// The fix is to subdivide in *world* space and project every vertex. All four corners of
// every quad drawn here are coplanar, so a bilinear blend of them stays on the real
// surface, and each cell ends up small enough that affine is invisible. Blish gets this
// from the GPU for free; a 2D draw list has to pay for it. How far to split is
// affineQuadSubdivisions, which lives with the projection because that is what it is about.
constexpr float kQuadPixelTolerance = 0.5f;

// What is left of a quad once the near plane has had it, ready to draw and to click.
//
// A quad only ever gets *drawn* through this, because a marker large enough to matter is
// routinely one you are standing inside: its centre goes behind you while most of it is
// still on screen. Dropping a quad because a corner — or the centre — is behind the camera
// is a marker that vanishes as you walk up to it, which is the one thing an editor must not
// do. Blish never faces the question; the GPU clips for it.
struct VisibleQuad {
    int count = 0; // 0 when the whole quad is behind the near plane, else 3..5
    glm::vec2 screen[5];
    float depth[5];
};

// The quad's corner depths come back too: every cell of the subdivision below re-clips
// against them, so they are worth computing once.
bool clipAndProject(const Camera& camera, const glm::vec3 world[4], float out_corner_depth[4], VisibleQuad* out) {
    camera.quadNearDepths(world, out_corner_depth);

    glm::vec2 parameter[5]; // where each surviving corner sits in the quad's own (u,v)
    out->count = clipQuadRegion(out_corner_depth, glm::vec2(0.0f), glm::vec2(1.0f), parameter);
    if (out->count < 3) return false;

    for (int i = 0; i < out->count; ++i) {
        // Everything here is at or past the near plane, so the projection cannot fail.
        if (!camera.project(bilinearPoint(world, parameter[i]), &out->screen[i], &out->depth[i])) return false;
    }
    return true;
}

void toScreen(const glm::vec2* points, int count, ImVec2* out) {
    for (int i = 0; i < count; ++i) out[i] = ImVec2(points[i].x, points[i].y);
}

// One corner of a subdivision cell, blended out of the quad it belongs to.
struct GridVertex {
    ImVec2 position;
    ImVec2 coord;
    float depth = 0.0f;
};

GridVertex quadVertex(const Camera& camera, const glm::vec3 world[4], const ImVec2 uv[4], const float corner_depth[4], glm::vec2 at) {
    float weight[4];
    bilinearWeights(at, weight);

    GridVertex vertex;
    glm::vec3 position(0.0f);
    ImVec2 coord(0.0f, 0.0f);
    for (int i = 0; i < 4; ++i) {
        position += world[i] * weight[i];
        coord.x += uv[i].x * weight[i];
        coord.y += uv[i].y * weight[i];
        vertex.depth += corner_depth[i] * weight[i];
    }

    // Projected unconditionally rather than only when the depth says it is in front. A
    // vertex the clip put exactly *on* the near plane can come back a hair negative, and
    // skipping it would leave a corner sitting at the screen origin — a stray triangle
    // across the whole viewport. Only vertices already known to be in front are read, so a
    // failure here never reaches the buffer.
    glm::vec2 projected(0.0f);
    camera.project(position, &projected);
    vertex.position = ImVec2(projected.x, projected.y);
    vertex.coord = coord;
    return vertex;
}

// Draws the visible part of one textured quad, subdividing as far as the perspective across
// it needs and clipping each cell that straddles the near plane.
void drawTexturedQuad(
    const Camera& camera,
    ImDrawList* list,
    ImTextureID texture,
    const glm::vec3 world[4],
    const float corner_depth[4],
    const VisibleQuad& visible,
    const ImVec2 uv[4],
    ImU32 colour) {
    if ((colour & IM_COL32_A_MASK) == 0) return; // what AddImageQuad checks for us below
    if (visible.count < 3) return;

    const bool whole =
        visible.count == 4 && corner_depth[0] >= 0.0f && corner_depth[1] >= 0.0f && corner_depth[2] >= 0.0f && corner_depth[3] >= 0.0f;
    const int steps = affineQuadSubdivisions(visible.screen, visible.depth, visible.count, kQuadPixelTolerance);

    if (whole && steps <= 1) {
        ImVec2 corners[4];
        toScreen(visible.screen, 4, corners);
        list->AddImageQuad(texture, corners[0], corners[1], corners[2], corners[3], uv[0], uv[1], uv[2], uv[3], colour);
        return;
    }

    // The grid covers the whole quad, not just the visible part, so that its cells tile:
    // cells wholly behind the near plane are skipped and the few it cuts are clipped. A
    // straddling quad always lands on the step cap, since a corner sitting on the near
    // plane makes the depth spread enormous — which is exactly where the cells need to be
    // smallest anyway.
    constexpr int kMaxStride = kMaxQuadSubdivisions + 1;
    GridVertex grid[kMaxStride * kMaxStride];
    const int stride = steps + 1;
    const float span = 1.0f / static_cast<float>(steps);

    for (int row = 0; row <= steps; ++row) {
        for (int column = 0; column <= steps; ++column) {
            grid[row * stride + column] = quadVertex(camera, world, uv, corner_depth, glm::vec2(column * span, row * span));
        }
    }

    // One draw command for the whole grid: AddImageQuad per cell would push and pop the
    // texture id each time, and that is a command each. A clipped cell can come out a
    // pentagon, so a quad the plane cuts reserves the worst case and hands the rest back;
    // one it does not is exact, which is the common case by a long way.
    const int reserved_vertices = steps * steps * (whole ? 4 : 5);
    const int reserved_indices = steps * steps * (whole ? 6 : 9);
    int used_vertices = 0;
    int used_indices = 0;

    list->PushTextureID(texture);
    list->PrimReserve(reserved_indices, reserved_vertices);

    for (int row = 0; row < steps; ++row) {
        for (int column = 0; column < steps; ++column) {
            const int corner[4] = {
                row * stride + column,
                row * stride + column + 1,
                (row + 1) * stride + column + 1,
                (row + 1) * stride + column};

            int behind = 0;
            for (int i = 0; i < 4; ++i) behind += grid[corner[i]].depth < 0.0f ? 1 : 0;
            if (behind == 4) continue;

            if (behind == 0) {
                const GridVertex& a = grid[corner[0]];
                const GridVertex& b = grid[corner[1]];
                const GridVertex& c = grid[corner[2]];
                const GridVertex& d = grid[corner[3]];
                list->PrimQuadUV(a.position, b.position, c.position, d.position, a.coord, b.coord, c.coord, d.coord, colour);
                used_vertices += 4;
                used_indices += 6;
                continue;
            }

            glm::vec2 clipped[5];
            const glm::vec2 low(column * span, row * span);
            const int clipped_count = clipQuadRegion(corner_depth, low, low + glm::vec2(span), clipped);
            if (clipped_count < 3) continue;

            // A fan from the first corner: the cell is convex, and one plane cannot make it
            // otherwise.
            const unsigned int base = list->_VtxCurrentIdx;
            for (int i = 0; i < clipped_count; ++i) {
                const GridVertex vertex = quadVertex(camera, world, uv, corner_depth, clipped[i]);
                list->PrimWriteVtx(vertex.position, vertex.coord, colour);
            }
            for (int i = 2; i < clipped_count; ++i) {
                list->PrimWriteIdx(static_cast<ImDrawIdx>(base));
                list->PrimWriteIdx(static_cast<ImDrawIdx>(base + i - 1));
                list->PrimWriteIdx(static_cast<ImDrawIdx>(base + i));
            }
            used_vertices += clipped_count;
            used_indices += (clipped_count - 2) * 3;
        }
    }

    list->PrimUnreserve(reserved_indices - used_indices, reserved_vertices - used_vertices);
    list->PopTextureID();
}

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
//
// Note it takes rotate="90" the way applyRotateList does — one component, the rest zero —
// rather than demanding all three. Blish is the lenient one here, so we are too.
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

} // namespace

bool markerTransform(const pack::Pack& pack, const pack::Pathable& pathable, MarkerTransform* out) {
    if (pathable.type != pack::PathableType::Poi) return false;

    // A POI without a position is an invisible anchor; the loader drops those and so do we.
    // See docs/format.md.
    const std::string* x = pathable.attributes.value("xpos");
    const std::string* y = pathable.attributes.value("ypos");
    const std::string* z = pathable.attributes.value("zpos");
    if (!x || !y || !z) return false;

    glm::vec3 position(0.0f);
    if (!pack::parseFloat(*x, &position.x)) return false;
    if (!pack::parseFloat(*y, &position.y)) return false;
    if (!pack::parseFloat(*z, &position.z)) return false;

    out->height_offset = pack.effectiveFloat(pathable, "heightoffset");
    // Where it is drawn, not its bare coordinate — which is what the gizmo has to sit on.
    position.y += out->height_offset;
    out->position = position;
    out->has_rotation = resolveRotation(pack, pathable, &out->rotation);
    return true;
}

namespace {

void rebuild(editor::State& state) {
    cache.markers.clear();
    cache.trails.clear();
    cache.revision = state.revision;

    for (size_t i = 0; i < state.pack.pathables.size(); ++i) {
        const pack::Pathable& pathable = state.pack.pathables[i];
        if (pathable.grave.deleted) continue;
        if (pathable.type == pack::PathableType::Route) continue; // deprecated, never drawn

        if (pathable.type == pack::PathableType::Poi) {
            const int map_id = state.pack.effectiveInt(pathable, "mapid");

            MarkerTransform transform;
            if (!markerTransform(state.pack, pathable, &transform)) continue;

            MarkerDraw marker;
            marker.position = transform.position;
            marker.has_rotation = transform.has_rotation;
            marker.rotation = transform.rotation;

            marker.pathable = i;
            marker.map_id = map_id;
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

        // map_id deliberately left unknown: it comes from the .trl, which the draw loop
        // resolves once the file is past the load budget.
        TrailDraw trail;
        trail.pathable = i;
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
void rotatedMarkerQuad(const MarkerDraw& marker, glm::vec3 out[4]) {
    const float half = marker.icon_size; // the quad is the unit square scaled by size*2
    const float cx = std::cos(marker.rotation.x), sx = std::sin(marker.rotation.x);
    const float cy = std::cos(marker.rotation.y), sy = std::sin(marker.rotation.y);
    const float cz = std::cos(marker.rotation.z), sz = std::sin(marker.rotation.z);

    for (int i = 0; i < 4; ++i) {
        glm::vec3 p(kQuadCorners[i].x * half, kQuadCorners[i].y * half, 0.0f);

        // X, then Y, then Z, matching the reference's matrix order.
        p = glm::vec3(p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx);
        p = glm::vec3(p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy);
        p = glm::vec3(p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z);

        // Blish Z-up -> file/Mumble Y-up.
        out[i] = marker.position + glm::vec3(p.x, p.z, p.y);
    }
}

// Blish derives each UV from the sign of the vertex it belongs to:
//     new Vector2(vert.X < 0 ? 1 : 0, vert.Y < 0 ? 1 : 0)
// so in its own space the -X,-Y corner carries uv (1,1) and the +X,+Y corner (0,0) —
// both axes inverted relative to the obvious (0,0)->(1,1) mapping, which is exactly a
// 180-degree in-plane rotation of the image. That is what a rotated marker gets.
// (ImVec2 predates constexpr constructors in the imgui Nexus ships, hence const.)
const ImVec2 kRotatedUv[4] = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};

// A billboard gets the plain mapping instead, and the difference is not a choice. Its
// corners come from Matrix.CreateBillboard, whose `right` axis points to screen *left*
// in Blish's right-handed space and to screen right in Mumble's left-handed one (see
// Camera::billboardCorners). Building the same basis in our space therefore flips u back,
// leaving only the vertical inversion — which the corner order absorbs.
const ImVec2 kBillboardUv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};

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

void drawMarkers(editor::State& state, const Camera& camera, ImDrawList* list, std::vector<Sorted>& order) {
    for (const Sorted& entry : order) {
        if (entry.is_trail) continue;

        const MarkerDraw& marker = cache.markers[entry.index];

        // Blish scales the unit quad by iconSize * 2, for either kind of marker.
        const float world_size = marker.icon_size * 2.0f;

        // Cheap reject before any quad work, and the *only* thing here that decides on the
        // marker's centre. A quad `world_size` across reaches at most 0.71 of that from its
        // centre whatever way it is turned, so a centre a full width behind the camera
        // cannot have any of itself in front of one. Rejecting on the centre alone — which
        // is what this used to do — is what made a large marker vanish the moment you
        // walked past its middle, with most of it still on screen.
        if (glm::dot(marker.position - camera.position, camera.front) < -world_size) continue;

        const float opacity = marker.alpha * fadeFactor(metresToInches(entry.distance), marker.fade_near, marker.fade_far);
        if (opacity <= 0.003f) continue;

        const bool selected =
            state.settings.highlight_3d && state.selection.contains(editor::NodeRef{editor::NodeKind::Pathable, marker.pathable});
        const ImU32 colour = applyOpacity(marker.tint, opacity);
        ImTextureID texture = resources::texture(marker.icon);

        glm::vec3 world_quad[4];
        float corner_depth[4];
        VisibleQuad visible;
        ImVec2 corners[5];

        if (marker.has_rotation) {
            // Fixed in the world, so it foreshortens with the viewing angle. minSize and
            // maxSize do not apply: those clamp a billboard's screen size, and clamping a
            // world-locked quad would detach it from the surface it is placed on.
            rotatedMarkerQuad(marker, world_quad);
            if (!clipAndProject(camera, world_quad, corner_depth, &visible)) continue;
            toScreen(visible.screen, visible.count, corners);

            if (texture) {
                drawTexturedQuad(camera, list, texture, world_quad, corner_depth, visible, kRotatedUv, colour);
            } else {
                list->AddConvexPolyFilled(corners, visible.count, colour);
            }

            markerQuadOverlay(list, corners, visible.count, marker.pathable, selected);
            continue;
        }

        // Billboarded towards the camera and projected — see Camera::billboardCorners for
        // why neither the size nor the shape of that follows from the distance alone.
        if (!camera.billboardQuad(marker.position, world_size, world_quad)) continue;

        // Blish then clamps the projected height between minSize*4 and maxSize*4, which is
        // twice the real pixel height either side (see kBlishSizeLimit), and satisfies the
        // clamp by rescaling the model matrix about the marker — so the quad is rebuilt at
        // the new size rather than stretched, or its shape would not be Blish's.
        //
        // A quad straddling the near plane has no projected height to measure and is drawn
        // at its true size instead. Blish's own screen bounds are meaningless there too,
        // and inventing a height would resize a marker for walking into it.
        glm::vec2 unclipped[4];
        bool measurable = true;
        for (int i = 0; i < 4 && measurable; ++i) measurable = camera.project(world_quad[i], &unclipped[i]);

        if (measurable) {
            const float measured = quadPixelHeight(unclipped);
            const float pixels = std::clamp(measured, marker.min_size * kBlishSizeLimit, marker.max_size * kBlishSizeLimit);
            if (pixels < 1.0f) continue;

            if (measured > 0.0001f && std::abs(pixels - measured) > 0.001f) {
                if (!camera.billboardQuad(marker.position, world_size * pixels / measured, world_quad)) continue;
            }
        }

        if (!clipAndProject(camera, world_quad, corner_depth, &visible)) continue;
        toScreen(visible.screen, visible.count, corners);

        glm::vec2 centre;
        if (texture) {
            drawTexturedQuad(camera, list, texture, world_quad, corner_depth, visible, kBillboardUv, colour);
        } else if (camera.project(marker.position, &centre)) {
            // No icon, or it failed to load: a dot still shows the marker is there. A dot
            // and not the whole quad, deliberately — an icon-less marker scaled up should
            // not blanket the screen with a solid colour.
            const float radius = std::max(2.0f, quadPixelHeight(visible.screen, visible.count) * 0.175f);
            list->AddCircleFilled(ImVec2(centre.x, centre.y), radius, applyOpacity(marker.tint, opacity * 0.7f));
        }

        markerQuadOverlay(list, corners, visible.count, marker.pathable, selected);
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

        // Clipped rather than skipped: a trail you are standing on has the segment under
        // your feet straddling the near plane, and dropping it leaves a gap in the ribbon
        // exactly where you are looking.
        const glm::vec3 world_quad[4] = {a - offset, a + offset, b + offset, b - offset};
        float corner_depth[4];
        VisibleQuad visible;
        if (!clipAndProject(camera, world_quad, corner_depth, &visible)) {
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

        ImVec2 corners[5];
        toScreen(visible.screen, visible.count, corners);

        if (texture) {
            // A segment running away from the camera has its two ends at very different
            // depths, which is the case the subdivision above exists for: without it the
            // texture slides along the ribbon instead of compressing into the distance.
            const ImVec2 uv[4] = {ImVec2(1.0f, v0), ImVec2(0.0f, v0), ImVec2(0.0f, v1), ImVec2(1.0f, v1)};
            drawTexturedQuad(camera, list, texture, world_quad, corner_depth, visible, uv, colour);
        } else {
            list->AddConvexPolyFilled(corners, visible.count, colour);
        }

        travelled += length;
    }
}

// No editor::State: a ribbon is drawn from its cached attributes and nothing else. Which
// trails are in `order` at all is decided in draw(), selection included.
void drawTrails(const Camera& camera, ImDrawList* list, std::vector<Sorted>& order) {
    for (const Sorted& entry : order) {
        if (!entry.is_trail) continue;

        const TrailDraw& trail = cache.trails[entry.index];
        const pack::trl::Trail* data = resources::trail(trail.data);
        if (!data) continue;

        ImTextureID texture = resources::texture(trail.texture);

        // Selecting a trail deliberately leaves the ribbon alone. Repainting a whole trail
        // gold is a lot of screen to change for one bit of information, and it hides the very
        // thing you selected it to look at — its texture, its tint, where it fades. The point
        // handles in drawTrailPoints are the selection feedback.
        for (const pack::trl::Segment& segment : data->segments) {
            drawTrailSection(camera, list, segment, trail, texture);
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

void draw(editor::State& state, const Camera& camera) {
    hover_highlight = state.settings.select_in_viewport ? hovered_pathable.load() : pack::kNoIndex;
    hovered_pathable.store(pack::kNoIndex);
    hover_point_highlight = state.settings.select_in_viewport ? hovered_trail_point.load() : pack::kNoIndex;
    hovered_trail_point.store(pack::kNoIndex);

    // With the map up the world is not on screen; markers drawn over it would sit at
    // meaningless positions. Bailing after the hover reset above also means nothing stays
    // hovered, so a click on the map cannot select what was underneath it.
    if (!state.loaded || !camera.valid || camera.map_open) return;

    if (cache.revision != state.revision) rebuild(state);

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
        TrailDraw& trail = cache.trails[i];

        // Its map lives in the .trl header, so the file has to have been read. Cached in the
        // TrailDraw once it has: an already-parsed trail is a hash lookup that costs no
        // budget, so a rebuild re-resolving every trail is free, and only a genuinely unread
        // one has to wait its turn. Until then it is on no map, which is the same thing
        // drawTrails does with a trail whose geometry has not arrived.
        if (trail.map_id == kMapUnknown) {
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
