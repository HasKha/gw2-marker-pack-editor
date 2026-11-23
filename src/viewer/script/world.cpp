#include <glm/geometric.hpp>

#include <algorithm>
#include <optional>
#include <type_traits>

#include "viewer/registry.h"
#include "viewer/runtime.h"
#include "viewer/script/api.h"
#include "viewer/script/context.h"
#include "viewer/script/mumble.h"
#include "viewer/store.h"

namespace viewer::script {

namespace {

// ── Reaching the live map ─────────────────────────────────────────────────────

runtime::LiveMap& live(lua_State* L) {
    return *context(L).host.live;
}

const registry::CategoryTree& tree(lua_State* L) {
    return context(L).host.installed->tree;
}

runtime::LiveMarker& liveMarker(lua_State* L, const Marker& marker) {
    return live(L).markers.at(marker.index);
}

runtime::LiveTrail& liveTrail(lua_State* L, const Trail& trail) {
    return live(L).trails.at(trail.index);
}

// Where Pathing has a marker: its own position, in Blish's space, without heightOffset,
// which Pathing keeps apart and adds only when drawing.
glm::vec3 markerPosition(const BakedMarker& marker) {
    return toBlish(marker.position - glm::vec3(0.0f, marker.height_offset, 0.0f));
}

void setMarkerPosition(BakedMarker* marker, const glm::vec3& blish) {
    marker->position = fromBlish(blish) + glm::vec3(0.0f, marker->height_offset, 0.0f);
}

float distanceToPlayer(lua_State* L, const BakedMarker& marker) {
    return glm::distance(markerPosition(marker), context(L).host.mumble->player_position);
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// ── Shared between markers and trails ─────────────────────────────────────────

// A texture as a pathable can draw it. An asset id is a texture from the game's render
// service, which nothing here can fetch: it draws nothing, and the census says so.
Texture drawable(lua_State* L, Texture texture) {
    if (texture.asset_id >= 0) {
        const Location where = callerLocation(L);
        context(L).census.record("AsyncTexture2D.FromAssetId", where.source, where.line);
        texture.path.clear();
    }
    return texture;
}

// What `pathable.Texture = value` assigns: a texture, or nil for none.
Texture assignedTexture(lua_State* L, const sol::object& value, size_t own_pack) {
    if (value.get_type() == sol::type::lua_nil) return Texture{own_pack};
    if (!value.is<Texture>()) raise(L, "a texture is expected");
    return drawable(L, value.as<Texture>());
}

// SetTexture(path) reads from the pathable's own pack, as the TextureResourceManager Pathing
// gives it does; SetTexture(assetId) is the unsupported kind.
Texture textureArgument(lua_State* L, size_t own_pack, const sol::variadic_args& args) {
    if (args.size() > 0 && sol::object(args[0]).get_type() == sol::type::number) {
        return drawable(L, Texture{own_pack, {}, argInt(L, args, 0, "SetTexture")});
    }
    return Texture{own_pack, argString(L, args, 0, "SetTexture")};
}

template <class Live>
std::vector<size_t> matching(lua_State* L, const std::vector<Live>& entities, size_t node, bool get_all) {
    const registry::CategoryTree& categories = tree(L);
    const auto under = [&](size_t of) {
        if (node == Category::kRoot) return get_all;
        if (of == node) return true;
        if (!get_all) return false;
        for (size_t at = of; at < categories.nodes.size(); at = categories.nodes[at].parent) {
            if (at == node) return true;
        }
        return false;
    };

    std::vector<size_t> out;
    for (size_t i = 0; i < entities.size(); ++i) {
        if (!entities[i].removed && under(entities[i].node)) out.push_back(i);
    }
    return out;
}

sol::table arrayOf(sol::state_view lua, const std::vector<size_t>& indices, sol::object (*push)(sol::state_view, size_t)) {
    sol::table table = lua.create_table(static_cast<int>(indices.size()), 0);
    for (size_t i = 0; i < indices.size(); ++i) table[i + 1] = push(lua, indices[i]);
    return table;
}

BakedMarker& bakedMarker(lua_State* L, const Marker& marker) {
    return liveMarker(L, marker).marker;
}

BakedTrail& bakedTrail(lua_State* L, const Trail& trail) {
    return liveTrail(L, trail).trail;
}

// A plain field of a baked marker or trail, read and written as it is: one row per field
// rather than a getter and a setter written out for each. `Baked` finds the baked pathable
// behind the handle.
template <class Handle, auto Baked, auto Field>
Member<Handle> field(const char* name) {
    using Value = std::remove_reference_t<decltype(Baked(nullptr, std::declval<Handle&>()).*Field)>;
    return {
        .name = name,
        .get = [](sol::state_view lua, Handle& self) { return sol::make_object(lua, Baked(lua, self).*Field); },
        .set =
            [](sol::state_view lua, Handle& self, const sol::object& value) {
                if constexpr (std::is_same_v<Value, bool>) {
                    Baked(lua, self).*Field = value.as<bool>();
                } else {
                    Baked(lua, self).*Field = valueFloat(lua, value, "a number");
                }
            },
    };
}

// ── StandardMarker ────────────────────────────────────────────────────────────

void setRotation(lua_State* L, const Marker& self, const glm::vec3& rotation) {
    BakedMarker& marker = bakedMarker(L, self);
    marker.rotation = rotation;
    marker.has_rotation = true;
}

// SetRotX and friends keep the other two axes, or zero when the marker had no rotation.
template <int Axis>
sol::object setRotationAxis(sol::state_view lua, Marker& self, sol::variadic_args args) {
    const BakedMarker& marker = bakedMarker(lua, self);
    glm::vec3 rotation = marker.has_rotation ? marker.rotation : glm::vec3(0.0f);
    rotation[Axis] = argFloat(lua, args, 0, "SetRot");
    setRotation(lua, self, rotation);
    return sol::lua_nil;
}

template <int Axis>
sol::object setPositionAxis(sol::state_view lua, Marker& self, sol::variadic_args args) {
    BakedMarker& marker = bakedMarker(lua, self);
    glm::vec3 position = markerPosition(marker);
    position[Axis] = argFloat(lua, args, 0, "SetPos");
    setMarkerPosition(&marker, position);
    return sol::lua_nil;
}

// SetPos(x, y, z) or SetPos(vector), and the same for SetRot.
glm::vec3 vectorArguments(lua_State* L, const sol::variadic_args& args, const char* what) {
    if (args.size() >= 3) return {argFloat(L, args, 0, what), argFloat(L, args, 1, what), argFloat(L, args, 2, what)};
    return toVector(L, argument(args, 0), what);
}

const Member<Marker> kMarkerMembers[] = {
    {.name = "Guid",
     .get = [](sol::state_view lua, Marker& self) { return pushGuid(lua, bakedMarker(lua, self).guid); },
     .set = [](sol::state_view lua, Marker& self, const sol::object& value) { bakedMarker(lua, self).guid = toGuid(lua, value, "Guid"); }},
    {.name = "Category", .get = [](sol::state_view lua, Marker& self) { return pushCategory(lua, liveMarker(lua, self).node); }},
    {.name = "MapId",
     .get = [](sol::state_view lua, Marker& self) { return sol::make_object(lua, bakedMarker(lua, self).map_id); },
     .set = [](sol::state_view lua,
               Marker& self,
               const sol::object& value) { bakedMarker(lua, self).map_id = static_cast<int>(valueFloat(lua, value, "MapId")); }},
    {.name = "Position",
     .get = [](sol::state_view lua, Marker& self) { return pushVector(lua, markerPosition(bakedMarker(lua, self))); },
     .set = [](sol::state_view lua,
               Marker& self,
               const sol::object& value) { setMarkerPosition(&bakedMarker(lua, self), toVector(lua, value, "Position")); }},
    // Nullable in Pathing: nil for a marker that faces the camera, and nil makes it face it again.
    {.name = "RotationXyz",
     .get = [](sol::state_view lua, Marker& self) -> sol::object {
         const BakedMarker& marker = bakedMarker(lua, self);
         return marker.has_rotation ? pushVector(lua, marker.rotation) : sol::object(sol::lua_nil);
     },
     .set =
         [](sol::state_view lua, Marker& self, const sol::object& value) {
             if (value.get_type() == sol::type::lua_nil) {
                 bakedMarker(lua, self).has_rotation = false;
             } else {
                 setRotation(lua, self, toVector(lua, value, "RotationXyz"));
             }
         }},
    field<Marker, bakedMarker, &BakedMarker::icon_size>("Size"),
    field<Marker, bakedMarker, &BakedMarker::alpha>("Alpha"),
    field<Marker, bakedMarker, &BakedMarker::fade_near>("FadeNear"),
    field<Marker, bakedMarker, &BakedMarker::fade_far>("FadeFar"),
    field<Marker, bakedMarker, &BakedMarker::min_size>("MinSize"),
    field<Marker, bakedMarker, &BakedMarker::max_size>("MaxSize"),
    field<Marker, bakedMarker, &BakedMarker::trigger_range>("TriggerRange"),
    field<Marker, bakedMarker, &BakedMarker::in_game_visibility>("InGameVisibility"),
    field<Marker, bakedMarker, &BakedMarker::map_visibility>("MapVisibility"),
    field<Marker, bakedMarker, &BakedMarker::minimap_visibility>("MiniMapVisibility"),
    {.name = "HeightOffset",
     .get = [](sol::state_view lua, Marker& self) { return sol::make_object(lua, bakedMarker(lua, self).height_offset); },
     .set =
         [](sol::state_view lua, Marker& self, const sol::object& value) {
             BakedMarker& marker = bakedMarker(lua, self);
             const float offset = valueFloat(lua, value, "HeightOffset");
             marker.position.y += offset - marker.height_offset;
             marker.height_offset = offset;
         }},
    {.name = "Tint",
     .get = [](sol::state_view lua, Marker& self) { return pushColor(lua, bakedMarker(lua, self).tint); },
     .set = [](sol::state_view lua, Marker& self, const sol::object& value) { bakedMarker(lua, self).tint = toColor(lua, value, "Tint"); }},
    {.name = "Texture",
     .get =
         [](sol::state_view lua, Marker& self) {
             const runtime::LiveMarker& marker = liveMarker(lua, self);
             return sol::make_object(lua, Texture{marker.texture_pack, marker.marker.icon});
         },
     .set =
         [](sol::state_view lua, Marker& self, const sol::object& value) {
             runtime::LiveMarker& marker = liveMarker(lua, self);
             const Texture texture = assignedTexture(lua, value, marker.pack);
             marker.texture_pack = texture.pack;
             marker.marker.icon = texture.path;
         }},
    {.name = "TipName",
     .get = [](sol::state_view lua, Marker& self) { return sol::make_object(lua, bakedMarker(lua, self).tip_name); },
     .set = [](sol::state_view lua, Marker& self, const sol::object& value) { bakedMarker(lua, self).tip_name = valueString(lua, value); }},
    {.name = "TipDescription",
     .get = [](sol::state_view lua, Marker& self) { return sol::make_object(lua, bakedMarker(lua, self).tip_description); },
     .set = [](sol::state_view lua,
               Marker& self,
               const sol::object& value) { bakedMarker(lua, self).tip_description = valueString(lua, value); }},
    {.name = "DistanceToPlayer",
     .get = [](sol::state_view lua, Marker& self) { return sol::make_object(lua, distanceToPlayer(lua, bakedMarker(lua, self))); }},
    {.name = "SetPos",
     .call =
         [](sol::state_view lua, Marker& self, sol::variadic_args args) {
             setMarkerPosition(&bakedMarker(lua, self), vectorArguments(lua, args, "SetPos"));
             return sol::object(sol::lua_nil);
         }},
    {.name = "SetPosX", .call = setPositionAxis<0>},
    {.name = "SetPosY", .call = setPositionAxis<1>},
    {.name = "SetPosZ", .call = setPositionAxis<2>},
    {.name = "SetRot",
     .call =
         [](sol::state_view lua, Marker& self, sol::variadic_args args) {
             setRotation(lua, self, vectorArguments(lua, args, "SetRot"));
             return sol::object(sol::lua_nil);
         }},
    {.name = "SetRotX", .call = setRotationAxis<0>},
    {.name = "SetRotY", .call = setRotationAxis<1>},
    {.name = "SetRotZ", .call = setRotationAxis<2>},
    {.name = "SetTexture",
     .call =
         [](sol::state_view lua, Marker& self, sol::variadic_args args) {
             runtime::LiveMarker& marker = liveMarker(lua, self);
             const Texture texture = textureArgument(lua, marker.pack, args);
             marker.texture_pack = texture.pack;
             marker.marker.icon = texture.path;
             return sol::object(sol::lua_nil);
         }},
    {.name = "Remove",
     .call =
         [](sol::state_view lua, Marker& self, sol::variadic_args) {
             liveMarker(lua, self).removed = true;
             return sol::object(sol::lua_nil);
         }},
};

// ── StandardTrail ─────────────────────────────────────────────────────────────

const Member<Trail> kTrailMembers[] = {
    {.name = "Guid",
     .get = [](sol::state_view lua, Trail& self) { return pushGuid(lua, bakedTrail(lua, self).guid); },
     .set = [](sol::state_view lua, Trail& self, const sol::object& value) { bakedTrail(lua, self).guid = toGuid(lua, value, "Guid"); }},
    {.name = "Category", .get = [](sol::state_view lua, Trail& self) { return pushCategory(lua, liveTrail(lua, self).node); }},
    {.name = "MapId", .get = [](sol::state_view lua, Trail& self) { return sol::make_object(lua, bakedTrail(lua, self).map_id); }},
    field<Trail, bakedTrail, &BakedTrail::alpha>("Alpha"),
    field<Trail, bakedTrail, &BakedTrail::fade_near>("FadeNear"),
    field<Trail, bakedTrail, &BakedTrail::fade_far>("FadeFar"),
    field<Trail, bakedTrail, &BakedTrail::in_game_visibility>("InGameVisibility"),
    field<Trail, bakedTrail, &BakedTrail::map_visibility>("MapVisibility"),
    field<Trail, bakedTrail, &BakedTrail::minimap_visibility>("MiniMapVisibility"),
    {.name = "Tint",
     .get = [](sol::state_view lua, Trail& self) { return pushColor(lua, bakedTrail(lua, self).tint); },
     .set = [](sol::state_view lua, Trail& self, const sol::object& value) { bakedTrail(lua, self).tint = toColor(lua, value, "Tint"); }},
    {.name = "TrailSampleColor",
     .get = [](sol::state_view lua, Trail& self) { return pushColor(lua, bakedTrail(lua, self).sample_colour); },
     .set = [](sol::state_view lua,
               Trail& self,
               const sol::object& value) { bakedTrail(lua, self).sample_colour = toColor(lua, value, "TrailSampleColor"); }},
    {.name = "Texture",
     .get =
         [](sol::state_view lua, Trail& self) {
             const runtime::LiveTrail& trail = liveTrail(lua, self);
             return sol::make_object(lua, Texture{trail.texture_pack, trail.trail.texture});
         },
     .set =
         [](sol::state_view lua, Trail& self, const sol::object& value) {
             runtime::LiveTrail& trail = liveTrail(lua, self);
             const Texture texture = assignedTexture(lua, value, trail.pack);
             trail.texture_pack = texture.pack;
             trail.trail.texture = texture.path;
         }},
    {.name = "SetTexture",
     .call =
         [](sol::state_view lua, Trail& self, sol::variadic_args args) {
             runtime::LiveTrail& trail = liveTrail(lua, self);
             const Texture texture = textureArgument(lua, trail.pack, args);
             trail.texture_pack = texture.pack;
             trail.trail.texture = texture.path;
             return sol::object(sol::lua_nil);
         }},
    {.name = "Remove",
     .call =
         [](sol::state_view lua, Trail& self, sol::variadic_args) {
             liveTrail(lua, self).removed = true;
             return sol::object(sol::lua_nil);
         }},
};

// ── PathingCategory ───────────────────────────────────────────────────────────

// Null for the root, which has no node of its own.
const registry::Category* nodeOf(lua_State* L, const Category& self) {
    return self.node == Category::kRoot ? nullptr : &tree(L).nodes.at(self.node);
}

std::string namespaceOf(lua_State* L, size_t node) {
    const registry::CategoryTree& categories = tree(L);
    std::string out;
    for (size_t at = node; at < categories.nodes.size(); at = categories.nodes[at].parent) {
        out = out.empty() ? categories.nodes[at].name : categories.nodes[at].name + "." + out;
    }
    return out;
}

// The root reads as Pathing's root does: no name, toggled on, nothing to say.
template <class Value>
sol::object nodeField(sol::state_view lua, const Category& self, Value registry::Category::* field, Value root_value) {
    const registry::Category* node = nodeOf(lua, self);
    return sol::make_object(lua, node ? node->*field : root_value);
}

void setVisible(lua_State* L, const Category& self, bool visible) {
    if (const registry::Category* node = nodeOf(L, self)) context(L).host.category_states->set(node->id, visible, node->default_toggle);
}

const Member<Category> kCategoryMembers[] = {
    {.name = "Name",
     .get = [](sol::state_view lua, Category& self) { return nodeField(lua, self, &registry::Category::name, std::string()); }},
    {.name = "DisplayName",
     .get = [](sol::state_view lua, Category& self) { return nodeField(lua, self, &registry::Category::label, std::string()); }},
    {.name = "Namespace",
     .get =
         [](sol::state_view lua, Category& self) {
             return sol::make_object(lua, self.node == Category::kRoot ? std::string() : namespaceOf(lua, self.node));
         }},
    {.name = "IsSeparator",
     .get = [](sol::state_view lua, Category& self) { return nodeField(lua, self, &registry::Category::is_separator, false); }},
    {.name = "DefaultToggle",
     .get = [](sol::state_view lua, Category& self) { return nodeField(lua, self, &registry::Category::default_toggle, true); }},
    {.name = "IsHidden",
     .get = [](sol::state_view lua, Category& self) { return nodeField(lua, self, &registry::Category::is_hidden, false); }},
    {.name = "Root", .get = [](sol::state_view lua, Category& self) { return sol::make_object(lua, self.node == Category::kRoot); }},
    // A top-level category's parent is the root, as in Pathing; the root's is nil.
    {.name = "Parent",
     .get = [](sol::state_view lua, Category& self) -> sol::object {
         const registry::Category* node = nodeOf(lua, self);
         if (!node) return sol::lua_nil;
         return pushCategory(lua, node->parent == pack::kNoIndex ? Category::kRoot : node->parent);
     }},
    // Pathing's CategoryStates.GetCategoryInactive: this category's own state, not its parents'.
    {.name = "IsVisible",
     .call =
         [](sol::state_view lua, Category& self, sol::variadic_args) {
             const registry::Category* node = nodeOf(lua, self);
             return sol::make_object(lua, !node || context(lua).host.category_states->enabled(node->id, node->default_toggle));
         }},
    {.name = "Show",
     .call =
         [](sol::state_view lua, Category& self, sol::variadic_args) {
             setVisible(lua, self, true);
             return sol::object(sol::lua_nil);
         }},
    {.name = "Hide",
     .call =
         [](sol::state_view lua, Category& self, sol::variadic_args) {
             setVisible(lua, self, false);
             return sol::object(sol::lua_nil);
         }},
    {.name = "GetMarkers",
     .call =
         [](sol::state_view lua, Category& self, sol::variadic_args args) {
             return sol::object(arrayOf(lua, matching(lua, live(lua).markers, self.node, argBool(args, 0)), pushMarker));
         }},
    {.name = "GetTrails",
     .call =
         [](sol::state_view lua, Category& self, sol::variadic_args args) {
             return sol::object(arrayOf(lua, matching(lua, live(lua).trails, self.node, argBool(args, 0)), pushTrail));
         }},
};

// ── World ─────────────────────────────────────────────────────────────────────

struct World {
    static constexpr const char* kName = "World";
    static std::span<const Member<World>> members();
};

// Markers then trails, in load order, as PackState.Entities lists them.
std::vector<std::pair<bool, size_t>> pathablesWithGuid(lua_State* L, const Guid& guid) {
    std::vector<std::pair<bool, size_t>> out;
    const runtime::LiveMap& map = live(L);
    for (size_t i = 0; i < map.markers.size(); ++i) {
        if (!map.markers[i].removed && map.markers[i].marker.guid == guid) out.push_back({true, i});
    }
    for (size_t i = 0; i < map.trails.size(); ++i) {
        if (!map.trails[i].removed && map.trails[i].trail.guid == guid) out.push_back({false, i});
    }
    return out;
}

sol::object pushPathable(sol::state_view lua, const std::pair<bool, size_t>& pathable) {
    return pathable.first ? pushMarker(lua, pathable.second) : pushTrail(lua, pathable.second);
}

// GetClosestMarker([category]) and GetClosestMarkers([category,] quantity): live markers,
// nearest the player first, of exactly that category when there is one.
std::vector<size_t> closestMarkers(lua_State* L, const sol::variadic_args& args, bool with_quantity) {
    std::optional<size_t> category;
    if (args.size() > 0 && sol::object(args[0]).is<Category>()) category = sol::object(args[0]).as<Category>().node;

    const runtime::LiveMap& map = live(L);
    std::vector<std::pair<float, size_t>> by_distance;
    for (size_t i = 0; i < map.markers.size(); ++i) {
        const runtime::LiveMarker& marker = map.markers[i];
        if (marker.removed || (category && marker.node != *category)) continue;
        by_distance.push_back({distanceToPlayer(L, marker.marker), i});
    }
    std::stable_sort(by_distance.begin(), by_distance.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    const size_t quantity = with_quantity ? static_cast<size_t>(std::max(0, argInt(L, args, category ? 1 : 0, "GetClosestMarkers"))) : 1;
    std::vector<size_t> out;
    for (size_t i = 0; i < by_distance.size() && i < quantity; ++i) out.push_back(by_distance[i].second);
    return out;
}

const Member<World> kWorldMembers[] = {
    {.name = "RootCategory", .get = [](sol::state_view lua, World&) { return pushCategory(lua, Category::kRoot); }},
    // TryGetCategoryFromNamespace, which ignores case.
    {.name = "CategoryByType",
     .call =
         [](sol::state_view lua, World&, sol::variadic_args args) {
             return pushCategory(lua, tree(lua).find(lowercase(argString(lua, args, 0, "CategoryByType"))));
         }},
    {.name = "PathableByGuid",
     .call = [](sol::state_view lua, World&, sol::variadic_args args) -> sol::object {
         const auto found = pathablesWithGuid(lua, toGuid(lua, argument(args, 0), "PathableByGuid"));
         return found.empty() ? sol::object(sol::lua_nil) : pushPathable(lua, found.front());
     }},
    {.name = "PathablesByGuid",
     .call =
         [](sol::state_view lua, World&, sol::variadic_args args) {
             const auto found = pathablesWithGuid(lua, toGuid(lua, argument(args, 0), "PathablesByGuid"));
             sol::table table = lua.create_table(static_cast<int>(found.size()), 0);
             for (size_t i = 0; i < found.size(); ++i) table[i + 1] = pushPathable(lua, found[i]);
             return sol::object(table);
         }},
    {.name = "MarkerByGuid",
     .call = [](sol::state_view lua, World&, sol::variadic_args args) -> sol::object {
         const auto found = pathablesWithGuid(lua, toGuid(lua, argument(args, 0), "MarkerByGuid"));
         return !found.empty() && found.front().first ? pushMarker(lua, found.front().second) : sol::object(sol::lua_nil);
     }},
    {.name = "TrailByGuid",
     .call = [](sol::state_view lua, World&, sol::variadic_args args) -> sol::object {
         const auto found = pathablesWithGuid(lua, toGuid(lua, argument(args, 0), "TrailByGuid"));
         return !found.empty() && !found.front().first ? pushTrail(lua, found.front().second) : sol::object(sol::lua_nil);
     }},
    {.name = "GetClosestMarker",
     .call = [](sol::state_view lua, World&, sol::variadic_args args) -> sol::object {
         const std::vector<size_t> closest = closestMarkers(lua, args, false);
         return closest.empty() ? sol::object(sol::lua_nil) : pushMarker(lua, closest.front());
     }},
    {.name = "GetClosestMarkers",
     .call = [](sol::state_view lua,
                World&,
                sol::variadic_args args) { return sol::object(arrayOf(lua, closestMarkers(lua, args, true), pushMarker)); }},
};

std::span<const Member<World>> World::members() {
    return kWorldMembers;
}

// One userdata per handle, kept for the life of the state.
template <class Handle>
sol::object pushCached(sol::state_view lua, std::unordered_map<size_t, sol::object>& cache, size_t index) {
    auto [it, inserted] = cache.try_emplace(index);
    if (inserted) it->second = sol::make_object(lua, Handle{index});
    return it->second;
}

} // namespace

std::span<const Member<Marker>> Marker::members() {
    return kMarkerMembers;
}

std::span<const Member<Trail>> Trail::members() {
    return kTrailMembers;
}

std::span<const Member<Category>> Category::members() {
    return kCategoryMembers;
}

void Category::extend(sol::usertype<Category>& type) {
    type[sol::meta_function::equal_to] = [](const Category& a, const Category& b) { return a.node == b.node; };
}

sol::object pushMarker(sol::state_view lua, size_t index) {
    return pushCached<Marker>(lua, context(lua).marker_objects, index);
}

sol::object pushTrail(sol::state_view lua, size_t index) {
    return pushCached<Trail>(lua, context(lua).trail_objects, index);
}

sol::object pushCategory(sol::state_view lua, size_t node) {
    if (node == pack::kNoIndex) return sol::lua_nil;
    return pushCached<Category>(lua, context(lua).category_objects, node);
}

void bindWorld(sol::state_view lua) {
    bindType<Marker>(lua);
    bindType<Trail>(lua);
    bindType<Category>(lua);
    bindType<World>(lua);
    context(lua).env["World"] = World{};
}

} // namespace viewer::script
