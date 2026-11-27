#pragma once

#include <Mumble.h>
#include <good_render.h>

#include <glm/vec2.hpp>

#include "viewer/game/camera.h"
#include "viewer/maps/map_scene.h"
#include "viewer/render/scene.h"

// The sink that draws the published scenes into the game's frame through GoodRender: the world at
// MOMENT_WORLD_FINISHED through world_quad, the map at MOMENT_BEFORE_WINDOWS. Optional: whatever no
// moment drew, the Nexus render draws instead.
namespace viewer::good_render_sink {

// Whether GoodRender is calling the moments: no service (not loaded, not loaded yet, or unable to
// follow the frame), one that does not serve the Moment version this was built against, or one
// that does.
enum class Service { None, OtherVersion, Serving };

// onMoment subscribed while this lives, whether or not the service is loaded yet: it calls once it
// is. The map is placed by `mumble` as the moment finds it. One at a time: the callback has no
// slot for which, so `mumble` is kept where it can find it.
class Subscription
{
public:
    explicit Subscription(const Mumble::Data* mumble);
    // Unsubscribes and waits for a call in progress to return. Never from inside a moment.
    ~Subscription();

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Service service() const;

private:
    good_render::Link link;
    good_render::Slot* slot = nullptr;
};

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

// What the moments of the frame being presented said of the full map: from PreRender, as every
// moment of a frame comes before its Present, and before the Render's takeDrawn() starts the next.
// Meaningful only while the service serves us (Subscription::service()).
map_scene::GameFrame frameMap();

// The callback a Subscription subscribes: the published scenes into whichever moment this is. Apart so a
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
