#pragma once

#include <Mumble.h>
#include <good_render.h>

#include <glm/vec2.hpp>

#include "viewer/camera.h"
#include "viewer/scene.h"

// The sink that draws the published scenes into the game's frame through GoodRender: the world at
// MOMENT_WORLD_FINISHED through world_quad, the map at MOMENT_BEFORE_WINDOWS. Optional: whatever no
// moment drew, the Nexus render draws instead.
namespace viewer::good_render_sink {

// Subscribes, whether or not the service is loaded yet: it calls once it is. The map is
// placed by `mumble` as the moment finds it.
void load(const Mumble::Data* mumble);

// Unsubscribes and waits for a call in progress to return. Never from inside a moment.
void unload();

// Whether GoodRender is calling the moments: no service (not loaded, not loaded yet, or unable to
// follow the frame), one that does not serve the Moment version this was built against, or one
// that does.
enum class Service { None, OtherVersion, Serving };
Service service();

// The milliseconds spent in our moments since the last call, for the slow-frame log.
double takeMomentMilliseconds();

// What the moments drew since the last call. The Nexus render at the end of the frame draws in
// the overlay what they did not: every moment of a frame comes before its Present, so what the
// service reached has already been drawn.
struct Drawn {
    bool occluded = false; // the world's items something of the world hides
    bool over = false; // the world's other items
    bool map = false; // the minimap; the full map is always the overlay's
    // A WORLD_FINISHED moment came, drawing or not: the game drew its world. It stops while the
    // full map is fully up (map_scene::FullMapZoom).
    bool world = false;
};
Drawn takeDrawn();

// The callback load() subscribes: the published scenes into whichever moment this is. Apart so a
// test can run a frame's moments of its own.
void onMoment(const good_render::Moment* moment);

// A moment's camera as our own, for the projection min/max size needs. `viewport` is in window
// pixels, the space minSize and maxSize are in.
Camera cameraFromMoment(const good_render::Camera& camera, glm::vec2 viewport);

// Hands all of `scene` to the moment's world_quad, in order, with billboards sized for the
// moment's camera, each hidden behind what its Occlusion names.
// WORLD_FINISHED's body, apart so a test can give it a moment of its own.
void submit(const good_render::Moment& moment, const Scene& scene);

// Draws what of `scene` nothing hides into the moment's draw list: WORLD_FINISHED's body
// when the moment has no world_quad. Flat, so a textured quad is subdivided as the overlay's are.
void drawOver(const good_render::Moment& moment, const Scene& scene);

} // namespace viewer::good_render_sink
