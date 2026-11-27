#pragma once

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <vector>

#include "viewer/game/mumble.h"
#include "viewer/live/runtime.h"
#include "viewer/maps/map.h"
#include "viewer/maps/map_scene.h"
#include "viewer/render/recycled.h"
#include "viewer/render/scene.h"

namespace viewer {

// The minimap and the full map across frames: every map's rectangles, each one's fade after it
// comes up, the full map's zoom as it opens and closes, the compass as MumbleLink last described
// it, each layer's trails laid on the continent, and the scenes published.
class Maps
{
public:
    // Reads the rectangles cached in `cache_file`, and asks the API for them on a worker.
    void open(const std::filesystem::path& cache_file);
    // Waits for the request and publishes nothing. After GoodRender's moments have stopped.
    void stop();

    // The map's rectangles, or null for a map neither the API nor the cache knows.
    const map::MapDetails* find(int map_id) const { return catalog.find(map_id); }

    // What a frame at Present hands the maps.
    struct Frame {
        const MumbleState* mumble = nullptr;
        bool in_gameplay = true;
        bool world_drawn = false; // a GoodRender moment drew the world in the frame before
        // What GoodRender's moments said of the full map in the frame being presented: none without
        // a service serving us (good_render_sink::frameMap).
        std::optional<map_scene::GameFrame> game_map;
        double seconds = 0.0;

        const runtime::LiveMap* live = nullptr;
        const std::vector<char>* visible = nullptr;
        TextureLookup texture;
        // The pack open in the editor, if any, whose markers belong to no node of the tree.
        const runtime::LiveMap* edited = nullptr;
        TextureLookup edited_texture;

        map_scene::Options options;
        float ui_scale = 1.0f;
        // The images the maps mark a far icon with.
        ImTextureID above = nullptr;
        ImTextureID below = nullptr;
    };

    // Publishes what the minimap shows this frame, and the full map while it is up. Nothing on a
    // map the API has not described, as in Pathing, nor on a loading screen.
    void build(const Frame& frame);

    // Whether the full map published last opens and closes as the game said, rather than estimated.
    bool fullMapFromGame() const { return full_map_from_game; }

private:
    map::Catalog catalog;
    map_scene::FadeIn minimap_fade;
    map_scene::FadeIn full_map_fade;
    map_scene::FullMapZoom full_map_zoom;
    bool full_map_from_game = false;
    map_scene::CompassKeeper compass;
    map_scene::TrailCache trails;
    map_scene::TrailCache edited_trails;
    Recycled<map_scene::MapScene> minimap_scenes;
    Recycled<map_scene::MapScene> full_map_scenes;
};

} // namespace viewer
