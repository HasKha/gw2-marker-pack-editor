// How long the world scene takes to build and submit over the corpus's busiest maps, offline: the
// numbers a change to scene.* or the GoodRender sink is judged by, without the game. Hidden from
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
#include <map>
#include <unordered_map>
#include <vector>

#include "corpus.h"
#include "util/job_queue.h"
#include "viewer/registry.h"
#include "viewer/render/good_render.h"
#include "viewer/runtime.h"
#include "viewer/scene.h"
#include "viewer/store.h"
#include "viewer/textures.h"

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
// as in game, and the packs' own caches between the scene and it, as in game.
std::unordered_map<std::string, Texture> made;

Texture* madeTexture(const char* identifier) {
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

    std::printf("%-6s %8s %8s %8s %10s %10s %8s %8s\n", "map", "points", "markers", "trails", "build ms", "submit ms", "quads", "runs");
    for (const auto& [map_id, point_count] : busiest) {
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

        // Over and over, untimed, until every image asked for is made, a few megabytes a frame
        // as in game: whatever is built or read on first sight is.
        const auto loading = [&installed] {
            size_t count = 0;
            for (const auto& pack : installed.packs) count += pack->textures.counts().loading;
            return count;
        };
        do {
            for (const viewer::Camera& camera : cameras) {
                uploads.nextFrame();
                viewer::buildScene(live, visible, camera, 1.0f, options, lookup, &scene);
                jobs.wait();
            }
        } while (loading() > 0);

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
            "%-6d %8zu %8zu %8zu %10.3f %10.3f %8zu %8zu\n",
            map_id,
            point_count,
            live.markers.size(),
            live.trails.size(),
            build_ms / frames,
            submit_ms / frames,
            quad_total / cameras.size(),
            run_total / cameras.size());
    }
}
