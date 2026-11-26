// How long the world scene takes to build and submit over the corpus's busiest maps, offline: the
// numbers a change to scene.*, map_scene.*, the live map or the GoodRender sink is judged by, without the game. Hidden from
// ctest; run it on a Release build, before and after, on one machine:
//
//     bin/Release/markus_viewer_test.exe "[.bench]"
//
// Every corpus pack is installed at once, which is more than a player has, so the busiest maps
// here are busier than in game. The views are the player standing on a trail of the map, the
// camera behind and above as the game's is, turned four ways: what a frame costs while playing
// the map, not the worst view of it.

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/type_ptr.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "corpus.h"
#include "draw_list.h"

#include <imgui_internal.h>
#include "util/job_queue.h"
#include "viewer/live/runtime.h"
#include "viewer/maps/map.h"
#include "viewer/maps/map_scene.h"
#include "viewer/packs/registry.h"
#include "viewer/render/good_render.h"
#include "viewer/render/scene.h"
#include "viewer/render/textures.h"
#include "viewer/store.h"

namespace {

// The camera as finishView makes it, `back` metres behind and `up` above the player, level.
viewer::Camera followCamera(const glm::vec3& player, float yaw) {
    constexpr float kBack = 6.0f;
    constexpr float kUp = 3.0f;
    viewer::Camera camera;
    camera.valid = true;
    camera.player = player;
    camera.front = glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
    camera.position = player - camera.front * kBack + glm::vec3(0.0f, kUp, 0.0f);
    camera.viewport = glm::vec2(1920.0f, 1080.0f);
    camera.aspect = camera.viewport.x / camera.viewport.y;
    camera.view = glm::lookAtLH(camera.position, camera.position + camera.front, camera.up);
    camera.projection = glm::perspectiveLH(camera.fov, camera.aspect, camera.near_plane, 10000.0f);
    camera.view_projection = camera.projection * camera.view;
    return camera;
}

// Nexus, making every image at once: one texture per identifier, so the sink's runs are counted
// as in game, and the packs' own caches between the scene and it, as in game. Under a lock, as Nexus's
// is: the caches make textures on the worker while the frame looks them up, and a rehash under a lookup
// hung or crashed this test.
std::mutex made_mutex;
std::unordered_map<std::string, Texture> made;

Texture* madeTexture(const char* identifier) {
    const std::lock_guard<std::mutex> lock(made_mutex);
    auto [it, inserted] = made.try_emplace(identifier);
    if (inserted) it->second = Texture{32, 32, reinterpret_cast<void*>(static_cast<uintptr_t>(made.size() * 16))};
    return &it->second;
}

// What the sink handed to world_quad: quads, and runs of one texture hidden behind one thing,
// which GoodRender draws one at a time.
size_t quads = 0;
size_t runs = 0;
void* last_texture = nullptr;
good_render::HiddenBehind last_hidden = good_render::HIDDEN_BEHIND_SCENE;

void countQuad(const float[4][3], const float[4][2], void* texture, uint32_t, good_render::HiddenBehind hidden_behind) {
    if (quads == 0 || texture != last_texture || hidden_behind != last_hidden) ++runs;
    ++quads;
    last_texture = texture;
    last_hidden = hidden_behind;
}

good_render::Camera momentCamera(const viewer::Camera& camera) {
    good_render::Camera out{};
    for (int i = 0; i < 3; ++i) {
        out.position[i] = camera.position[i];
        out.front[i] = camera.front[i];
        out.up[i] = camera.up[i];
    }
    out.fov = camera.fov;
    out.aspect = camera.aspect;
    std::copy_n(glm::value_ptr(camera.view), 16, out.view);
    std::copy_n(glm::value_ptr(camera.projection), 16, out.projection);
    std::copy_n(glm::value_ptr(camera.view_projection), 16, out.view_projection);
    return out;
}

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

TEST_CASE("the scene over the corpus's busiest maps, timed", "[.bench]") {
    // Maps measured, from the most trail points down, and how often each view is built: enough
    // that a frame's cost is read to a few microseconds.
    constexpr size_t kMaps = 4;
    constexpr size_t kViewsPerMap = 8;
    constexpr int kRepeats = 25;

    const std::vector<std::filesystem::path> paths = corpus::packs();
    if (paths.empty()) FAIL(corpus::missingMessage());

    AddonAPI api{};
    api.Textures.GetOrCreateFromMemory = [](const char* identifier, void*, size_t) { return madeTexture(identifier); };
    api.Textures.Get = [](const char* identifier) { return madeTexture(identifier); };
    util::JobQueue jobs;
    viewer::TextureUploads uploads;
    viewer::runtime::Installed installed;
    installed.api = &api;
    installed.jobs = &jobs;
    installed.uploads = &uploads;
    for (const std::filesystem::path& path : paths) {
        auto loaded = std::make_unique<viewer::registry::LoadedPack>(viewer::registry::loadPack(path));
        viewer::runtime::install(&installed, std::move(loaded));
    }
    const viewer::store::CategoryStates states;
    const std::vector<char> visible = viewer::runtime::visibleNodes(installed.tree, states);

    // Trail points per map, over every pack.
    std::map<int, size_t> points_on;
    for (const std::unique_ptr<viewer::runtime::InstalledPack>& pack : installed.packs) {
        for (const viewer::BakedTrail& trail : pack->loaded->pathables.trails) {
            if (!trail.geometry) continue;
            for (const pack::trl::Segment& segment : trail.geometry->segments) points_on[trail.map_id] += segment.points.size();
        }
    }
    std::vector<std::pair<int, size_t>> busiest(points_on.begin(), points_on.end());
    std::sort(busiest.begin(), busiest.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    busiest.resize(std::min(busiest.size(), kMaps));

    const viewer::TextureLookup lookup = [&installed](size_t pack, const std::string& path) {
        return viewer::runtime::packImage(installed, pack, path);
    };
    const viewer::WorldOptions options;
    viewer::Scene scene;

    // Lines as Nexus's ImGui draws them, which its NewFrame sets up: anti-aliased, from the baked lines in the font
    // atlas where a width is whole, with vertex offsets for more than 64k vertices.
    viewer::test::DrawListFixture draw_list;
    ImGui::GetIO().Fonts->Build();
    ImDrawListSharedData* shared = ImGui::GetDrawListSharedData();
    shared->TexUvWhitePixel = ImGui::GetIO().Fonts->TexUvWhitePixel;
    shared->TexUvLines = ImGui::GetIO().Fonts->TexUvLines;
    shared->InitialFlags = ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex | ImDrawListFlags_AllowVtxOffset;
    std::printf(
        "%-6s %8s %8s %8s %10s %10s %10s %10s %8s %8s\n",
        "map",
        "points",
        "markers",
        "trails",
        "live ms",
        "build ms",
        "submit ms",
        "minimap ms",
        "quads",
        "runs");
    for (const auto& [map_id, point_count] : busiest) {
        // The live map, as a map change or a pack coming or going builds it.
        constexpr int kLiveMaps = 5;
        const auto lived = std::chrono::steady_clock::now();
        for (int i = 0; i < kLiveMaps; ++i) viewer::runtime::liveMap(installed, map_id);
        const double live_ms = millisecondsSince(lived) / kLiveMaps;
        const viewer::runtime::LiveMap live = viewer::runtime::liveMap(installed, map_id);

        // The player at points spread along the map's trails, in the order they were loaded.
        std::vector<glm::vec3> trail_points;
        for (const viewer::runtime::LiveTrail& trail : live.trails) {
            if (!trail.trail.geometry) continue;
            for (const pack::trl::Segment& segment : trail.trail.geometry->segments) {
                trail_points.insert(trail_points.end(), segment.points.begin(), segment.points.end());
            }
        }
        REQUIRE_FALSE(trail_points.empty());
        std::vector<viewer::Camera> cameras;
        for (size_t view = 0; view < kViewsPerMap; ++view) {
            const glm::vec3 player = trail_points[view * trail_points.size() / kViewsPerMap];
            for (int turn = 0; turn < 4; ++turn) cameras.push_back(followCamera(player, static_cast<float>(turn) * 1.5708f));
        }

        // The minimap at each view, and the full map, on a map rectangle of the test's own: where on the continent the
        // map lands does not change what is built or drawn.
        viewer::map::MapDetails details;
        details.map_top_left = glm::dvec2(-100000.0, 100000.0);
        details.map_size = glm::dvec2(200000.0, 200000.0);
        details.continent_size = glm::dvec2(10000.0, 10000.0);
        viewer::map_scene::TrailCache map_trails;
        std::vector<viewer::MumbleState> compasses;
        for (const viewer::Camera& camera : cameras) {
            viewer::MumbleState mumble;
            mumble.available = true;
            mumble.map_id = map_id;
            mumble.compass_width = 300;
            mumble.compass_height = 300;
            mumble.map_scale = 1.0;
            // Blish HUD's axes, z up.
            mumble.player_position = glm::vec3(camera.player.x, camera.player.z, camera.player.y);
            mumble.camera_position = glm::vec3(camera.position.x, camera.position.z, camera.position.y);
            compasses.push_back(mumble);
        }
        glm::dvec2 low(std::numeric_limits<double>::max());
        glm::dvec2 high(std::numeric_limits<double>::lowest());
        for (const glm::vec3& point : trail_points) {
            const glm::dvec2 at = viewer::map::continentPosition(details, point);
            low = glm::min(low, at);
            high = glm::max(high, at);
        }
        constexpr glm::vec2 kDisplay(1920.0f, 1080.0f);
        const double fit = std::max((high.x - low.x) / kDisplay.x, (high.y - low.y) / kDisplay.y);
        viewer::MumbleState full_map;
        full_map.available = true;
        full_map.map_id = map_id;
        full_map.is_map_open = true;
        full_map.compass_width = 300;
        full_map.compass_height = 300;
        full_map.map_center = (low + high) * 0.5;
        full_map.player_position = glm::vec3(trail_points[0].x, trail_points[0].z, trail_points[0].y);
        full_map.map_scale = fit;
        viewer::map_scene::MapScene minimap;
        viewer::map_scene::MapScene full_scene;

        // Over and over, untimed, until every image asked for is made, a few megabytes a frame as in game: whatever is
        // built or read on first sight is. Every scene, since an image goes on only while it is looked up.
        const auto loading = [&installed] {
            size_t count = 0;
            for (const auto& pack : installed.packs) count += pack->textures.counts().loading;
            return count;
        };
        // At most this many passes, which a few hundred images handed over a few megabytes a frame take a small part
        // of: past it the run goes on, and says what never arrived, rather than waiting on it for good.
        constexpr int kMostPasses = 200;
        int passes = 0;
        for (; passes < kMostPasses && (passes == 0 || loading() > 0); ++passes) {
            for (const viewer::Camera& camera : cameras) {
                uploads.nextFrame();
                viewer::buildScene(live, visible, camera, 1.0f, options, lookup, &scene);
                viewer::map_scene::build(live, visible, details, compasses.front(), {}, 1.0f, 1.0f, lookup, &map_trails, &minimap);
                viewer::map_scene::build(live, visible, details, full_map, {}, 1.0f, 1.0f, lookup, &map_trails, &full_scene);
                jobs.wait();
            }
        }
        if (passes == kMostPasses) std::printf("       %zu images never arrived\n", loading());

        double minimap_ms = 0.0;
        for (const viewer::MumbleState& compass : compasses) {
            const auto mapped = std::chrono::steady_clock::now();
            for (int i = 0; i < kRepeats; ++i) {
                viewer::map_scene::build(live, visible, details, compass, {}, 1.0f, 1.0f, lookup, &map_trails, &minimap);
            }
            minimap_ms += millisecondsSince(mapped);
        }

        // The full map's trails and icons drawn, with the whole map fitting the screen, then smaller on it, as the map
        // zoomed out: what Nexus's render draws while it is up, every line the map has in however few pixels.
        std::printf("       full map:");
        for (const double zoom_out : {1.0, 2.0, 4.0}) {
            full_map.map_scale = fit * zoom_out;
            const std::optional<viewer::map::View> view = viewer::map::view(full_map, kDisplay, 1.0f);
            REQUIRE(view);
            ImDrawList& list = draw_list.list();
            const auto drawn = std::chrono::steady_clock::now();
            for (int i = 0; i < kRepeats; ++i) {
                list._ResetForNewFrame();
                list.PushClipRectFullScreen();
                viewer::map_scene::draw(&list, full_scene, *view, full_map.player_position.z);
            }
            std::printf(" x%.0f %.3f ms %d vertices;", zoom_out, millisecondsSince(drawn) / kRepeats, list.VtxBuffer.Size);
        }
        std::printf("\n");

        double build_ms = 0.0;
        double submit_ms = 0.0;
        size_t quad_total = 0;
        size_t run_total = 0;
        for (const viewer::Camera& camera : cameras) {
            const auto built = std::chrono::steady_clock::now();
            for (int i = 0; i < kRepeats; ++i) {
                viewer::buildScene(live, visible, camera, 1.0f + 0.016f * static_cast<float>(i), options, lookup, &scene);
            }
            build_ms += millisecondsSince(built);

            const good_render::Camera moment_camera = momentCamera(camera);
            good_render::Moment moment{};
            moment.moment = good_render::MOMENT_WORLD_FINISHED;
            moment.width = 1920;
            moment.height = 1080;
            moment.pixels_per_window_pixel = 1.0f;
            moment.camera = &moment_camera;
            moment.world_quad = countQuad;
            const auto submitted = std::chrono::steady_clock::now();
            for (int i = 0; i < kRepeats; ++i) {
                quads = 0;
                runs = 0;
                viewer::good_render_sink::submit(moment, scene);
            }
            submit_ms += millisecondsSince(submitted);
            quad_total += quads;
            run_total += runs;
        }
        const double frames = static_cast<double>(cameras.size() * kRepeats);
        std::printf(
            "%-6d %8zu %8zu %8zu %10.3f %10.3f %10.3f %10.3f %8zu %8zu\n",
            map_id,
            point_count,
            live.markers.size(),
            live.trails.size(),
            live_ms,
            build_ms / frames,
            submit_ms / frames,
            minimap_ms / frames,
            quad_total / cameras.size(),
            run_total / cameras.size());
    }
}
