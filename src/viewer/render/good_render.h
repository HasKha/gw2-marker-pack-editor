#pragma once

#include <good_render.h>

#include <glm/vec2.hpp>

#include "viewer/camera.h"
#include "viewer/scene.h"

// The sink that draws the published scene into the game's own frame through GoodRender, at
// MOMENT_WORLD_FINISHED: under the game's windows and HUD, and each pixel hidden behind what
// the game drew in front of it.
//
// GoodRender is optional. Subscribing costs nothing while no service is loaded, and every
// frame no moment draws — no service, a loading screen, the full map — the Nexus render
// callback draws the overlay instead.
namespace viewer::good_render_sink {

// Subscribes, whether or not the service is loaded yet: it calls once it is.
void load();

// Unsubscribes and waits for a call in progress to return. Never from inside a moment.
void unload();

// Whether a moment drew the scene since the last call. The Nexus render at the end of the
// frame draws the overlay only when this is false: every moment of a frame comes before its
// Present, so a frame the service reached has already been drawn.
bool takeDrawn();

// A moment's camera as our own, for the projection min/max size needs. `viewport` is in window
// pixels, the space minSize and maxSize are in.
Camera cameraFromMoment(const good_render::Camera& camera, glm::vec2 viewport);

// Hands `scene` to the moment's world_quad, in order, with billboards sized for the moment's
// camera. The moment callback's body, apart so a test can give it a moment of its own.
void submit(const good_render::Moment& moment, const Scene& scene);

} // namespace viewer::good_render_sink
