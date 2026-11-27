#include "viewer/maps/maps.h"

#include <memory>

namespace viewer {

namespace {

const std::vector<char> kNoNodes;

} // namespace

void Maps::open(const std::filesystem::path& cache_file) {
    catalog.start(cache_file);
}

void Maps::stop() {
    catalog.stop();
    map_scene::publish(nullptr, nullptr);
    minimap_scenes.clear();
    full_map_scenes.clear();
    trails = map_scene::TrailCache{};
    edited_trails = map_scene::TrailCache{};
}

void Maps::build(const Frame& frame) {
    const MumbleState& mumble = *frame.mumble;
    const map_scene::CompassKeeper::Kept kept = compass.update(mumble);
    // The compass stays where it was under the full map, so its fade goes on through it.
    const MumbleState on_compass = map_scene::withCompass(mumble, kept.compass);
    const float minimap_opacity = minimap_fade.update(on_compass, frame.seconds);
    const float full_map_opacity = full_map_fade.update(mumble, frame.seconds);
    const map_scene::FullMapZoom::Frame zoom = full_map_zoom.update(mumble.is_map_open, frame.world_drawn, frame.seconds, frame.game_map);
    full_map_from_game = mumble.is_map_open && zoom.game;
    // The game's fade is the map's whole fade, FlatMap's fade-in being an estimate of it.
    const float map_fade = zoom.game ? zoom.opacity : full_map_opacity * zoom.opacity;

    const std::shared_ptr<map_scene::MapScene> minimap = minimap_scenes.take();
    std::shared_ptr<map_scene::MapScene> full_map;
    if (const map::MapDetails* details = mumble.available && frame.in_gameplay ? catalog.find(mumble.map_id) : nullptr) {
        std::vector<map_scene::Layer> layers{map_scene::Layer{frame.live, frame.visible, frame.texture, &trails}};
        if (frame.edited) layers.push_back(map_scene::Layer{frame.edited, &kNoNodes, frame.edited_texture, &edited_trails, true});
        map_scene::build(layers, *details, on_compass, frame.options, minimap_opacity, frame.ui_scale, minimap.get());
        minimap->keep_compass = kept.keep;
        minimap->above = frame.above;
        minimap->below = frame.below;
        if (mumble.is_map_open) {
            full_map = full_map_scenes.take();
            map_scene::build(layers, *details, mumble, frame.options, map_fade, frame.ui_scale, full_map.get());
            full_map->zoom = zoom.zoom;
            full_map->above = frame.above;
            full_map->below = frame.below;
        }
    } else {
        *minimap = map_scene::MapScene{};
    }
    minimap_scenes.keep(minimap);
    if (full_map) full_map_scenes.keep(full_map);
    map_scene::publish(minimap, full_map);
}

} // namespace viewer
