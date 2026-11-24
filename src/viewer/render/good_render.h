#pragma once

#include <Mumble.h>
#include <good_render.h>

#include <glm/vec2.hpp>

#include "viewer/camera.h"
#include "viewer/scene.h"

// The sink that draws the published scenes into the game's own frame through GoodRender. The
// world at MOMENT_WORLD_FINISHED, under the game's whole UI, all of it through world_quad in the
// scene's order, each hidden behind what its Occlusion names of what the game drew in front of
// it. The GPU draws those, so a texture foreshortens as it should, which through
// a flat draw list it only does as far as the overlay's subdivision takes it. The map at
// MOMENT_BEFORE_WINDOWS, over the HUD and under the windows.
//
// GoodRender is optional. Subscribing costs nothing while no service is loaded, and every
// frame no moment draws a scene — no service, a loading screen — the Nexus render callback draws
// it instead.
namespace viewer::good_render_sink {

// Subscribes, whether or not the service is loaded yet: it calls once it is. The map is
// placed by `mumble` as the moment finds it.
void load(const Mumble::Data* mumble);

// Unsubscribes and waits for a call in progress to return. Never from inside a moment.
void unload();

// What the moments drew since the last call. The Nexus render at the end of the frame draws in
// the overlay what they did not: every moment of a frame comes before its Present, so what the
// service reached has already been drawn.
struct Drawn {
    bool occluded = false; // the world's items something of the world hides
    bool over = false; // the world's other items
    bool map = false;
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
