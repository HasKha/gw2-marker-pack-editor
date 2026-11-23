#include "viewer/render/good_render.h"

#include <glm/gtc/type_ptr.hpp>

#include <atomic>

#include "viewer/map_scene.h"

namespace viewer::good_render_sink {

namespace {

good_render::Link link;
good_render::Slot* slot = nullptr;

const Mumble::Data* mumble_link = nullptr;

// Set by the moment, taken by the Nexus render that ends the same frame.
std::atomic<bool> drawn{false};
std::atomic<bool> map_drawn{false};

void onMoment(const good_render::Moment* moment) {
    // In the game's UI, over its HUD, where the game draws its own map markers.
    if (moment->moment == good_render::MOMENT_BEFORE_WINDOWS && moment->draw_list) {
        const float scale = moment->pixels_per_window_pixel > 0.0f ? moment->pixels_per_window_pixel : 1.0f;
        map_scene::drawPublished(moment->draw_list, mumble_link, glm::vec2(moment->width / scale, moment->height / scale));
        map_drawn = true;
        return;
    }

    // The finished world, before any UI: markers belong under the HUD and the windows. Null
    // world_quad or camera means no world to draw into this frame — leave it to the fallback.
    if (moment->moment != good_render::MOMENT_WORLD_FINISHED || !moment->world_quad || !moment->camera) return;

    const std::shared_ptr<const Scene> scene = published();
    if (!scene) return;

    submit(*moment, *scene);
    drawn = true;
}

} // namespace

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

bool takeDrawn() {
    return drawn.exchange(false);
}

bool takeMapDrawn() {
    return map_drawn.exchange(false);
}

Camera cameraFromMoment(const good_render::Camera& source, glm::vec2 viewport) {
    Camera camera;
    camera.valid = true;
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
    // Up to WORLD_FINISHED what is bound is the scene, which resolution scaling makes smaller
    // than the window; minSize and maxSize are in window pixels.
    const float scale = moment.pixels_per_window_pixel > 0.0f ? moment.pixels_per_window_pixel : 1.0f;
    const Camera camera = cameraFromMoment(*moment.camera, glm::vec2(moment.width / scale, moment.height / scale));

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
        moment.world_quad(world, coords, item.texture, item.colour);
    }
}

} // namespace viewer::good_render_sink
