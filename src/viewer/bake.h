#pragma once

#include <imgui.h>

#include <glm/vec3.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pack/model.h"
#include "pack/trl.h"
#include "viewer/behaviors/attributes.h"
#include "viewer/camera.h"
#include "viewer/guid.h"

// A pack resolved for drawing: every inherited attribute looked up once, so a frame never
// walks the category chain.
//
// Every pack is drawn from this. The viewer bakes each installed pack once, on its worker, and the
// pack open in the editor again whenever State::revision moves. One reading of inheritance for
// both is what keeps what an author sees and what a player sees from disagreeing.
namespace viewer {

// Where a POI sits and how it is oriented, resolved through the category chain.
//
// The editor's gizmo places its handles with this too, because the two have to agree to the
// last digit: the handles are supposed to sit on the drawn icon, and a second copy of this
// logic is a second chance to disagree with the picture. (There were two, and they did — one
// accepted a partial rotate="90" the way Blish does, the other required all three components
// and silently read it as no rotation at all.)
struct MarkerTransform {
    glm::vec3 position{0.0f}; // as drawn, i.e. with heightOffset already applied
    glm::vec3 rotation{0.0f}; // radians, in the attributes' own axis order
    float height_offset = 0.0f;

    // Any of rotate / rotate-x / rotate-y / rotate-z turns the marker from a billboard into a
    // fixed-orientation quad — including rotate="0,0,0", which is not a no-op.
    bool has_rotation = false;
};

// False for anything that cannot be placed: a trail, or a POI missing a coordinate. Those are
// invisible anchors, and Pathing drops them too.
bool markerTransform(const pack::Pack& pack, const pack::Pathable& pathable, MarkerTransform* out);

// Where a marker or trail is drawn with GoodRender: `render-layer`, our own attribute. Default
// leaves it to the player's setting (WorldOptions::behind_terrain).
enum class RenderLayer : uint8_t {
    Default,
    Windows, // "windows": over the world, under the game's UI. Named when that UI was only its windows
    Terrain, // "terrain": behind whatever of the world is in front of it
};

// `render-layer`'s value, in any case; Default for anything else.
RenderLayer parseRenderLayer(std::string_view value);

// `cull`: Pathing's CullDirection, the rasterizer state a marker or trail is drawn with. Which
// face each keeps is named by the first triangle Blish draws, its first three vertices v0, v1,
// v2: CounterClockwise keeps the side cross(v1 - v0, v2 - v0) points to, worked out in our
// left-handed space, and Clockwise the other. That was read off Hero's pack rather than
// derived: its Aerodrome raid icons, all CounterClockwise, face the hub on that side, and it
// is the side their image reads unmirrored from. A billboard's first triangle faces away
// from the camera that way, so CounterClockwise always shows it and Clockwise never does.
enum class Cull : uint8_t { None, Clockwise, CounterClockwise };

struct BakedMarker {
    size_t pathable = pack::kNoIndex; // into Pack::pathables
    size_t category = pack::kNoIndex; // into Pack::categories; kNoIndex for a dangling type
    int map_id = 0;

    glm::vec3 position{0.0f}; // with heightOffset applied
    float height_offset = 0.0f; // already in `position`; a script sees and sets the position without it
    bool has_rotation = false; // see MarkerTransform
    glm::vec3 rotation{0.0f}; // radians

    float icon_size = 1.0f;
    float alpha = 1.0f;
    float min_size = 5.0f;
    float max_size = 2048.0f;
    float fade_near = -1.0f; // inches; negative turns the fade off
    float fade_far = -1.0f; // inches
    ImU32 tint = IM_COL32_WHITE;
    std::string icon;

    Guid guid; // random when the pack gives none, as Pathing's is
    bool in_game_visibility = true;
    bool map_visibility = true;
    bool minimap_visibility = true;
    RenderLayer render_layer = RenderLayer::Default;
    Cull cull = Cull::None;
    float map_display_size = 20.0f; // across, in Blish HUD's pixels at MapScale 1 — see map_scene.*
    bool scale_on_map_with_zoom = true;
    float trigger_range = 2.0f; // triggerrange, or inforange standing in for it (StandardMarker_Focus.cs)
    float reset_length = 0.0f; // seconds a ReappearAfterTimer marker hides for; scripts set it

    // What the marker's tooltip says: tip-name, else its category's display name.
    std::string tip_name;
    std::string tip_description;

    // Its filters and modifiers; null for the many markers that have none.
    std::shared_ptr<const behaviors::Behaviors> behaviors;
};

// No real map has this id, so it is safe as "not resolved yet" — see BakedTrail::map_id.
constexpr int kMapUnknown = -1;

struct BakedTrail {
    size_t pathable = pack::kNoIndex;
    size_t category = pack::kNoIndex;

    // **From the `.trl` header, not from the XML.** `UnpackTrailPoi` reads the header and then
    // does `AddOrUpdateAttribute("mapid", ...)`, overwriting whatever the XML said, so the
    // header is what decides which map Pathing draws a trail on and the attribute is dead
    // weight — see docs/format.md. So it is unknown until the file has been read, by
    // whoever reads it: the registry does so for every trail on its worker, the edited pack's
    // are read through the editor's cache as its load budget allows.
    int map_id = kMapUnknown;

    float alpha = 1.0f;
    float scale = 1.0f;
    float anim_speed = 1.0f; // texture lengths per second along the trail
    bool is_wall = false;
    float fade_near = 700.0f; // inches
    float fade_far = 900.0f; // inches
    ImU32 tint = IM_COL32_WHITE;
    std::string texture;
    std::string data;

    // The colour the trail is drawn with on the map: its tint when it has one, and white
    // until then. Pathing samples the texture for a trail with no tint; that comes with the
    // map (StandardTrail[texture].cs).
    ImU32 sample_colour = IM_COL32_WHITE;

    Guid guid;
    bool in_game_visibility = true;
    bool map_visibility = true;
    bool minimap_visibility = true;
    RenderLayer render_layer = RenderLayer::Default;
    Cull cull = Cull::None;

    std::shared_ptr<const behaviors::Behaviors> behaviors; // filters and script only; null for none

    // The points, once read: up front for an installed pack; for the edited pack, from the
    // editor's cache, which holds its unsaved edits, so that an edit shows at once.
    std::shared_ptr<const pack::trl::Trail> geometry;
};

struct BakedPathables {
    std::vector<BakedMarker> markers;
    std::vector<BakedTrail> trails;
};

// A category as one pack declares it, ready to merge into the tree every pack shares.
struct BakedCategory {
    std::string id; // full dotted path, lowercase: what packs are merged on
    std::string name; // its own segment as written: the label when no pack gives it one
    size_t parent = pack::kNoIndex; // into the same list

    // What this pack's declarations say, and nothing when they say nothing, so that merging
    // keeps whatever another pack said. TmfLib's PathableCategoryBuilder overrides only the
    // attributes a declaration has.
    std::optional<std::string> display_name;
    std::optional<bool> is_separator;
    std::optional<bool> default_toggle;
    std::optional<bool> is_hidden;
};

// One entry per Pack::categories, in the same order, so a baked pathable's `category` indexes
// this list too.
std::vector<BakedCategory> bakeCategories(const pack::Pack& pack);

// The marker `pathable` describes, resolved through `pack`'s categories. False for one that
// cannot be placed — see markerTransform. `pathable` need not be one of `pack.pathables`: a
// marker a script creates is baked against its pack's categories the same way.
bool bakeMarker(const pack::Pack& pack, const pack::Pathable& pathable, BakedMarker* out);

// Every live marker and trail of `pack`, in Pack::pathables order. Deleted pathables, routes
// and POIs without a position are left out.
BakedPathables bake(const pack::Pack& pack);

// How much of a fade is left at `distance_inches` from the player: 1 inside `fade_near`, 0
// past `fade_far`, linear between. A negative bound turns the fade off, as Pathing does by
// moving both to float.MaxValue (StandardMarker[fadenear,fadefar].cs).
float fadeFactor(float distance_inches, float fade_near, float fade_far);

// `colour` with its alpha multiplied by `opacity`, clamped to 0..1.
ImU32 applyOpacity(ImU32 colour, float opacity);

// The four world corners of a marker carrying `rotate`, in kQuadCorners order: a quad fixed in
// the world rather than turned towards the camera.
void rotatedMarkerQuad(const glm::vec3& position, const glm::vec3& rotation, float icon_size, glm::vec3 out[4]);

// Texture coordinates for a marker's corners in kQuadCorners order. Blish derives each from
// the sign of the vertex it belongs to,
//     new Vector2(vert.X < 0 ? 1 : 0, vert.Y < 0 ? 1 : 0)
// so in its own space the -X,-Y corner carries uv (1,1) and the +X,+Y corner (0,0) — both
// axes inverted, which is exactly a 180-degree in-plane rotation of the image. That is what a
// rotated marker gets.
extern const ImVec2 kRotatedMarkerUv[4];

// A billboard gets the plain mapping instead, and the difference is not a choice. Its corners
// come from Matrix.CreateBillboard, whose `right` axis points to screen *left* in Blish's
// right-handed space and to screen right in Mumble's left-handed one (see
// Camera::billboardCorners). Building the same basis in our space therefore flips u back,
// leaving only the vertical inversion — which the corner order absorbs.
extern const ImVec2 kBillboardUv[4];

// Blish HUD's StandardTrail.World.cs: TRAIL_WIDTH = 20 * 0.0254f, used as the half-width
// offset either side of the path, scaled by trailScale. One texture length along the trail is
// twice this, whatever the scale.
constexpr float kTrailHalfWidth = 20.0f * kMetresPerInch;

} // namespace viewer
