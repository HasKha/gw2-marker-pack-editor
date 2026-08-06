// Camera projection maths. Nothing here needs the game or a pack — a Camera is a handful
// of matrices and these are the closed-form answers Blish's renderer produces for the same
// inputs, which is the only way to check marker sizing without staring at the game.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>

#include "world/camera.h"

using Catch::Approx;

namespace {

constexpr float kFov = 1.222f; // ~70 degrees vertical, the value GW2 reports by default
constexpr float kWidth = 1920.0f;
constexpr float kHeight = 1080.0f;

// A camera at the origin looking down +Z with no roll and no pitch, so view space and
// world space coincide and the expected values below can be written by hand.
world::Camera axisCamera() {
    world::Camera camera;
    camera.position = glm::vec3(0.0f);
    camera.front = glm::vec3(0.0f, 0.0f, 1.0f);
    camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.viewport = glm::vec2(kWidth, kHeight);
    camera.aspect = kWidth / kHeight;
    camera.fov = kFov;
    camera.view = glm::lookAtLH(camera.position, camera.position + camera.front, camera.up);
    camera.projection = glm::perspectiveLH(camera.fov, camera.aspect, 0.1f, 10000.0f);
    camera.view_projection = camera.projection * camera.view;
    camera.valid = true;
    return camera;
}

// Pixels per world unit at one unit of *depth*: half the viewport spans tan(fov/2) at unit
// distance along the view axis.
float pixelsPerUnitAtUnitDepth() {
    return (kHeight * 0.5f) / std::tan(kFov * 0.5f);
}

float billboardHeight(const world::Camera& camera, const glm::vec3& position, float size) {
    glm::vec2 corners[4];
    REQUIRE(camera.billboardCorners(position, size, corners));
    return world::quadPixelHeight(corners);
}

} // namespace

TEST_CASE("a billboard on the optical axis is size over distance", "[camera]") {
    const world::Camera camera = axisCamera();

    // Straight ahead, depth and radial distance are the same thing, so this is the one
    // case where the naive formula is also right.
    const float height = billboardHeight(camera, glm::vec3(0.0f, 0.0f, 100.0f), 2.0f);

    CHECK(height == Approx(2.0f / 100.0f * pixelsPerUnitAtUnitDepth()).epsilon(0.001f));
}

TEST_CASE("a billboard off to the side is sized by depth, not radial distance", "[camera][regression]") {
    const world::Camera camera = axisCamera();

    // 40 degrees off-axis horizontally, which at this FOV is still on screen. The quad
    // faces the camera, so its up axis stays vertical and its projected height is
    // size/depth — radial distance is 1/cos(40) = 1.3x larger and sizing by it made
    // markers near the edge of the screen visibly small.
    const float size = 2.0f;
    const float depth = 100.0f;
    const float x = depth * std::tan(0.698f);
    const float radius = std::sqrt(x * x + depth * depth);
    const float height = billboardHeight(camera, glm::vec3(x, 0.0f, depth), size);

    // The quad's near edge is turned a little towards the camera, which puts it
    // (size/2)*x/r closer than the marker itself. That is exact, not a fudge — the same
    // 0.6% Blish's own projected bounding rectangle picks up.
    const float near_edge = depth - (size * 0.5f) * x / radius;
    CHECK(height == Approx(size / near_edge * pixelsPerUnitAtUnitDepth()).epsilon(0.002f));

    // And the thing that regressed: it must not shrink as it moves off the axis. Sizing
    // by radial distance made this 24% smaller than the on-axis marker.
    const float on_axis = billboardHeight(camera, glm::vec3(0.0f, 0.0f, depth), size);
    CHECK(height == Approx(on_axis).epsilon(0.01f));
    CHECK(height > on_axis);
}

TEST_CASE("a billboard above the axis tilts towards the camera and grows", "[camera][regression]") {
    const world::Camera camera = axisCamera();

    // Offset vertically instead, and the billboard's up axis tilts to face the camera.
    // The quad subtends size/r radians and lands on the image plane magnified by
    // 1/cos^2, so the projected height is size * r / depth^2 — larger than on-axis, where
    // the radial formula makes it smaller.
    const float depth = 100.0f;
    const float y = depth * std::tan(0.524f); // 30 degrees up
    const float radius = std::sqrt(y * y + depth * depth);
    const float height = billboardHeight(camera, glm::vec3(0.0f, y, depth), 2.0f);

    CHECK(height == Approx(2.0f * radius / (depth * depth) * pixelsPerUnitAtUnitDepth()).epsilon(0.005f));

    const float on_axis = billboardHeight(camera, glm::vec3(0.0f, 0.0f, depth), 2.0f);
    CHECK(height > on_axis);
}

TEST_CASE("billboard height scales linearly with size and inversely with depth", "[camera]") {
    const world::Camera camera = axisCamera();

    const float base = billboardHeight(camera, glm::vec3(0.0f, 0.0f, 50.0f), 2.0f);

    CHECK(billboardHeight(camera, glm::vec3(0.0f, 0.0f, 50.0f), 4.0f) == Approx(base * 2.0f).epsilon(0.001f));
    CHECK(billboardHeight(camera, glm::vec3(0.0f, 0.0f, 100.0f), 2.0f) == Approx(base * 0.5f).epsilon(0.001f));
}

TEST_CASE("a billboard straddling the camera plane fails to project", "[camera]") {
    const world::Camera camera = axisCamera();

    // In the camera's face: Blish draws it clipped and enormous, and the maxSize clamp is
    // what bounds it. The renderer needs to be told there is no quad to speak of.
    //
    // It has to be off to the side to straddle at all — a billboard is perpendicular to
    // the ray, so one dead ahead stays parallel to the camera plane however close it gets.
    glm::vec2 corners[4];
    CHECK_FALSE(camera.billboardCorners(glm::vec3(3.0f, 0.0f, 0.5f), 4.0f, corners));
    CHECK(camera.billboardCorners(glm::vec3(0.0f, 0.0f, 0.5f), 4.0f, corners));
}

TEST_CASE("a billboard is square and upright on the optical axis", "[camera][orientation]") {
    const world::Camera camera = axisCamera();

    glm::vec2 corners[4];
    REQUIRE(camera.billboardCorners(glm::vec3(0.0f, 0.0f, 100.0f), 2.0f, corners));

    // kQuadCorners order is (-right,-up), (+right,-up), (+right,+up), (-right,+up), and
    // screen y points down — so corners 0 and 1 are the *bottom* pair. The UV tables in
    // renderer.cpp are written against exactly this, so it is worth pinning.
    CHECK(corners[0].x < corners[1].x);
    CHECK(corners[3].x < corners[2].x);
    CHECK(corners[0].y > corners[3].y);
    CHECK(corners[1].y > corners[2].y);

    // Dead ahead there is no tilt, so it comes out an axis-aligned square.
    CHECK(corners[0].x == Approx(corners[3].x));
    CHECK(corners[1].x == Approx(corners[2].x));
    CHECK(corners[0].y == Approx(corners[1].y));
    CHECK((corners[1].x - corners[0].x) == Approx(corners[0].y - corners[3].y));
}

TEST_CASE("a billboard off the axis is tilted, not axis-aligned", "[camera][orientation]") {
    const world::Camera camera = axisCamera();

    // Up and to the side, which is where drawing an axis-aligned square visibly parted
    // company with Blish. The billboard turns to face the camera, so its top and bottom
    // edges are no longer level with each other on screen.
    glm::vec2 corners[4];
    REQUIRE(camera.billboardCorners(glm::vec3(40.0f, 30.0f, 60.0f), 4.0f, corners));

    const float bottom_tilt = corners[1].y - corners[0].y;
    const float top_tilt = corners[2].y - corners[3].y;

    CHECK(std::abs(bottom_tilt) > 1.0f);
    // Both edges lean the same way, which is what a rotation looks like as opposed to a
    // shear or a corner landing in the wrong slot.
    CHECK(bottom_tilt * top_tilt > 0.0f);

    // And it is still wound the same way round, so the UVs still land where they should.
    CHECK(corners[0].x < corners[1].x);
    CHECK(corners[0].y > corners[3].y);
}

TEST_CASE("project puts the view axis in the middle of the viewport", "[camera]") {
    const world::Camera camera = axisCamera();

    glm::vec2 screen;
    REQUIRE(camera.project(glm::vec3(0.0f, 0.0f, 10.0f), &screen));
    CHECK(screen.x == Approx(kWidth * 0.5f).margin(0.01f));
    CHECK(screen.y == Approx(kHeight * 0.5f).margin(0.01f));

    // Behind the camera has no meaningful screen position.
    CHECK_FALSE(camera.project(glm::vec3(0.0f, 0.0f, -10.0f), &screen));
}
