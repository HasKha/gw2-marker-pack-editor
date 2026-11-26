#include "viewer/render/good_render.h"

#include <glm/gtc/type_ptr.hpp>

#include <atomic>
#include <chrono>

#include "viewer/maps/map_scene.h"
#include "viewer/render/overlay.h"

namespace viewer::good_render_sink {

namespace {

good_render::Link link;
good_render::Slot* slot = nullptr;

const Mumble::Data* mumble_link = nullptr;

// Set by the moments, taken by the Nexus render that ends the same frame.
std::atomic<bool> occluded_drawn{false};
std::atomic<bool> over_drawn{false};
std::atomic<bool> map_drawn{false};
std::atomic<bool> world_moment{false};

// The time spent in the moments since takeMomentMilliseconds last took it.
std::atomic<int64_t> moment_nanoseconds{0};

// Adds the time from its making to its end to moment_nanoseconds, whichever way the moment returns.
struct MomentTimer {
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    ~MomentTimer() {
        moment_nanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count();
    }
};

// Up to WORLD_FINISHED what is bound is the scene, which resolution scaling makes smaller than
// the window; minSize and maxSize, and the draw list, are in window pixels.
glm::vec2 windowSize(const good_render::Moment& moment) {
    const float scale = moment.pixels_per_window_pixel > 0.0f ? moment.pixels_per_window_pixel : 1.0f;
    return glm::vec2(moment.width / scale, moment.height / scale);
}

} // namespace

void onMoment(const good_render::Moment* moment) {
    const MomentTimer timer;

    // In the game's UI, over its HUD: the minimap, where the game draws its own map markers.
    // Under the full map as it opens and closes over the compass. The full map is the overlay's:
    // the game draws it after this moment, and no moment comes while it is fully up.
    if (moment->moment == good_render::MOMENT_BEFORE_WINDOWS && moment->draw_list) {
        map_scene::drawPublished(moment->draw_list, mumble_link, windowSize(*moment), false);
        map_drawn = true;
        return;
    }

    if (moment->moment == good_render::MOMENT_WORLD_FINISHED) world_moment = true;

    // The finished world, before any UI, so the game's HUD and nameplates go over every marker:
    // all of it through world_quad, or, with no world_quad, what nothing hides into the draw
    // list. No camera means no world to draw into this frame — leave it to the fallback.
    // Over the full map too: GoodRender keeps the camera there, and the map is the game's UI,
    // drawn over this.
    if (moment->moment != good_render::MOMENT_WORLD_FINISHED || !moment->camera) return;

    const std::shared_ptr<const Scene> scene = published();
    if (!scene) return;

    if (moment->world_quad) {
        submit(*moment, *scene);
        occluded_drawn = true;
        over_drawn = true;
    } else if (moment->draw_list) {
        drawOver(*moment, *scene);
        over_drawn = true;
    }
}

void load(const Mumble::Data* mumble) {
    mumble_link = mumble;
    link = good_render::open();
    // Order 0, as GoodRender's README asks of anyone without a reason to draw over others.
    if (link.block) slot = good_render::subscribe(*link.block, onMoment, 0);
}

void unload() {
    if (slot) good_render::unsubscribe(*slot);
    slot = nullptr;
    good_render::close(link);
}

Service service() {
    if (!link.block || link.block->newest_served.load() == 0) return Service::None;
    return good_render::served(*link.block) ? Service::Serving : Service::OtherVersion;
}

double takeMomentMilliseconds() {
    return static_cast<double>(moment_nanoseconds.exchange(0)) / 1.0e6;
}

Drawn takeDrawn() {
    Drawn drawn;
    drawn.occluded = occluded_drawn.exchange(false);
    drawn.over = over_drawn.exchange(false);
    drawn.map = map_drawn.exchange(false);
    drawn.world = world_moment.exchange(false);
    return drawn;
}

Camera cameraFromMoment(const good_render::Camera& source, glm::vec2 viewport) {
    Camera camera;
    camera.valid = true;
    camera.source = CameraSource::GoodRender;
    camera.position = glm::make_vec3(source.position);
    camera.front = glm::make_vec3(source.front);
    camera.up = glm::make_vec3(source.up);
    camera.fov = source.fov;
    camera.aspect = source.aspect;
    // GoodRender's projection runs from 0.1 m (good_render.h), which is ours too; the
    // near-plane clip and the projection have to agree on it.
    camera.near_plane = 0.1f;
    camera.viewport = viewport;
    camera.view = glm::make_mat4(source.view);
    camera.projection = glm::make_mat4(source.projection);
    camera.view_projection = glm::make_mat4(source.view_projection);
    return camera;
}

void submit(const good_render::Moment& moment, const Scene& scene) {
    const Camera camera = cameraFromMoment(*moment.camera, windowSize(moment));

    for (const SceneItem& item : scene.items) {
        if (!item.texture) continue;
        // A billboard goes through world_quad as the overlay's quad, not through
        // world_billboard: Blish's billboard faces the ray to its own centre rather than the
        // camera's plane, and its size is clamped on its projected corners.
        glm::vec3 corners[4];
        const ImVec2* uv = nullptr;
        if (!itemCorners(camera, item, corners, &uv)) continue;

        float world[4][3];
        float coords[4][2];
        for (int i = 0; i < 4; ++i) {
            world[i][0] = corners[i].x;
            world[i][1] = corners[i].y;
            world[i][2] = corners[i].z;
            coords[i][0] = uv[i].x;
            coords[i][1] = uv[i].y;
        }
        // Through the GPU whatever hides it, even nothing, as Pathing draws everything: it
        // foreshortens a texture where a draw list cannot. In Occlusion's order.
        constexpr good_render::HiddenBehind kHiddenBehind[] = {
            good_render::HIDDEN_BEHIND_NOTHING,
            good_render::HIDDEN_BEHIND_CHARACTERS,
            good_render::HIDDEN_BEHIND_SCENE};
        moment.world_quad(world, coords, item.texture, item.colour, kHiddenBehind[static_cast<size_t>(item.occlusion)]);
    }
}

void drawOver(const good_render::Moment& moment, const Scene& scene) {
    overlay::drawScene(moment.draw_list, cameraFromMoment(*moment.camera, windowSize(moment)), scene, false, true);
}

} // namespace viewer::good_render_sink
